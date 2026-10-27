#include "../world_generation.h"
#include "../game.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_profile_samples(void) {
  static const struct { int x, z, top; BlockType surface; } samples[] = {
      {256, 256, 3, BLOCK_TYPE_GRASS}, {360, 256, 3, BLOCK_TYPE_GRASS},
      {374, 256, 2, BLOCK_TYPE_SAND}, {380, 256, 2, BLOCK_TYPE_SAND},
      {384, 256, 1, BLOCK_TYPE_SAND}, {386, 256, 1, BLOCK_TYPE_SAND},
      {388, 256, 0, BLOCK_TYPE_SAND}, {418, 256, -16, BLOCK_TYPE_SAND},
      {447, 256, -32, BLOCK_TYPE_STONE}, {448, 256, -32, BLOCK_TYPE_STONE},
      {0, 0, -32, BLOCK_TYPE_STONE}, {-1, 256, -32, BLOCK_TYPE_STONE},
      {256, -1, -32, BLOCK_TYPE_STONE}, {512, 256, -32, BLOCK_TYPE_STONE},
      {256, 512, -32, BLOCK_TYPE_STONE}, {INT_MIN, INT_MAX, -32, BLOCK_TYPE_STONE},
  };
  for (size_t i = 0; i < sizeof(samples) / sizeof(*samples); i++) {
    const TerrainColumn column = terrain_sample_column(1, samples[i].x, samples[i].z);
    const TerrainColumn otherSeed = terrain_sample_column(42, samples[i].x, samples[i].z);
    assert(column.stoneTopY == samples[i].top);
    assert(column.height == (samples[i].top > 1 ? samples[i].top : 1));
    assert(terrain_column_block(column, samples[i].top - 1) == samples[i].surface);
    assert(terrain_column_block(column, column.height - 1) == column.material);
    assert(column.height == otherSeed.height && column.stoneTopY == otherSeed.stoneTopY);
    assert(column.material == otherSeed.material && column.sandDepth == otherSeed.sandDepth &&
           column.soilDepth == otherSeed.soilDepth);
    assert(terrain_column_block(column, -129) == BLOCK_TYPE_EMPTY);
    assert(terrain_column_block(column, -128) == BLOCK_TYPE_DARK_STONE);
    assert(terrain_column_block(column, -65) == BLOCK_TYPE_DARK_STONE);
    assert(terrain_column_block(column, -64) == BLOCK_TYPE_STONE);
    assert(terrain_column_block(column, column.height) == BLOCK_TYPE_EMPTY);
    if (samples[i].surface == BLOCK_TYPE_GRASS) {
      assert(terrain_column_block(column, samples[i].top - 2) == BLOCK_TYPE_DIRT);
      assert(terrain_column_block(column, samples[i].top - 3) == BLOCK_TYPE_DIRT);
      assert(terrain_column_block(column, samples[i].top - 4) == BLOCK_TYPE_STONE);
    } else if (samples[i].surface == BLOCK_TYPE_SAND) {
      assert(terrain_column_block(column, samples[i].top - 2) == BLOCK_TYPE_SAND);
      assert(terrain_column_block(column, samples[i].top - 3) == BLOCK_TYPE_SAND);
      assert(terrain_column_block(column, samples[i].top - 4) == BLOCK_TYPE_STONE);
    }
    for (int y = -64; y < column.height; y++) {
      const BlockType type = terrain_column_block(column, y);
      assert(y < column.stoneTopY ? block_is_solid(type) : type == BLOCK_TYPE_WATER);
    }
  }
}

static void test_outline_and_boundaries(void) {
  size_t grass = 0, dry = 0, foothold = 0;
  for (int z = 0; z < 512; z++) {
    int dryRuns = 0, previousDry = 0;
    for (int x = 0; x < 512; x++) {
      const TerrainColumn column = terrain_sample_column(1, x, z);
      const TerrainColumn mirror = terrain_sample_column(1, 511 - x, 511 - z);
      assert(column.stoneTopY == mirror.stoneTopY && column.material == mirror.material);
      const int isDry = column.stoneTopY > 1;
      dryRuns += isDry && !previousDry;
      previousDry = isDry;
      grass += column.material == BLOCK_TYPE_GRASS;
      dry += isDry != 0;
      foothold += column.stoneTopY == 1;
      if (x) assert(abs(column.stoneTopY - terrain_sample_column(1, x - 1, z).stoneTopY) <= 1);
      if (z) assert(abs(column.stoneTopY - terrain_sample_column(1, x, z - 1).stoneTopY) <= 1);
      if (!x || !z || x == 511 || z == 511)
        assert(column.stoneTopY == -32 && column.material == BLOCK_TYPE_WATER);
    }
    assert(dryRuns <= 1); /* No disconnected fragments, holes, or concentric dry rings. */
  }
  assert(grass > 43000 && grass < 45000);
  assert(dry > 50000 && dry < 53000);
  assert(foothold > 2000 && foothold < 5000);
  printf("Experimental beach 01: %zu grass, %zu dry, %zu waterline columns\n", grass, dry, foothold);
}

static void compare_blocks(const World *const eager, const World *const streamed) {
  for (int x = eager->minX; x < eager->minX + eager->blocksPerSide; x++)
    for (int z = eager->minZ; z < eager->minZ + eager->blocksPerSide; z++)
      for (int y = -129; y <= 6; y++)
        assert(world_get_block(eager, x, y, z) == world_get_block(streamed, x, y, z));
}

static void compare_meshes(const World *const eager, const World *const streamed, const int cx, const int cz) {
  for (int sy = WORLD_MIN_SECTION_Y; sy <= 0; sy++) {
    SectionMesh a = {0}, b = {0};
    assert(section_mesh_build(eager, cx, sy, cz, &a));
    assert(section_mesh_build(streamed, cx, sy, cz, &b));
    assert(a.vertexCount == b.vertexCount);
    assert(!memcmp(a.typeVertexCount, b.typeVertexCount, sizeof(a.typeVertexCount)));
    if (a.vertexCount) assert(!memcmp(a.vertices, b.vertices, a.vertexCount * sizeof(*a.vertices)));
    section_mesh_destroy(&a);
    section_mesh_destroy(&b);
  }
}

static void test_materialization_and_edits(void) {
  static const int positions[][2] = {{256, 256}, {372, 256}, {382, 256}, {390, 256},
                                     {418, 256}, {446, 256}, {165, 165}, {127, 127}};
  for (size_t i = 0; i < sizeof(positions) / sizeof(*positions); i++) {
    const int x = positions[i][0], z = positions[i][1];
    const int cx = voxel_section_coord(x), cz = voxel_section_coord(z);
    World eager = {0}, streamed = {0};
    const Vec3 origin = {12.0f, -3.0f, 8.0f};
    assert(create_terrain_region(&eager, x - 17, z - 17, 35, origin, 0.5f, 1));
    assert(world_init_region(&streamed, x - 17, z - 17, 35, origin, 0.5f));
    streamed.proceduralTerrain = 1;
    streamed.terrainSeed = 1;
    compare_blocks(&eager, &streamed);
    compare_meshes(&eager, &streamed, cx, cz);
    const TerrainColumn column = terrain_sample_column(1, x, z);
    const float wx = origin.x + ((float)x + 0.5f) * 0.5f;
    const float wz = origin.z + ((float)z + 0.5f) * 0.5f;
    assert(world_support_height(&streamed, wx, wz) == origin.y + (float)column.stoneTopY * 0.5f);
    assert(world_surface_height(&streamed, wx, wz) == origin.y + (float)column.height * 0.5f);
    assert(world_set_block(&eager, x, column.stoneTopY - 1, z, BLOCK_TYPE_EMPTY));
    assert(world_set_block(&streamed, x, column.stoneTopY - 1, z, BLOCK_TYPE_EMPTY));
    assert(world_set_block(&eager, x, column.height + 2, z, BLOCK_TYPE_STONE));
    assert(world_set_block(&streamed, x, column.height + 2, z, BLOCK_TYPE_STONE));
    for (int dx = -1; dx <= 1; dx++)
      for (int dz = -1; dz <= 1; dz++)
        assert(world_materialize_chunk(&streamed, cx + dx, cz + dz));
    compare_blocks(&eager, &streamed);
    compare_meshes(&eager, &streamed, cx, cz);
    world_release_chunk(&streamed, cx, cz);
    assert(streamed.editCount == 2);
    compare_blocks(&eager, &streamed);
    compare_meshes(&eager, &streamed, cx, cz);
    assert(world_materialize_chunk(&streamed, cx, cz));
    compare_blocks(&eager, &streamed);
    world_destroy(&eager);
    world_destroy(&streamed);
  }
}

static float player_radius(const Player *const player) {
  const float dx = player->position.x - 256.0f, dz = player->position.z - 256.0f;
  return sqrtf(dx * dx + dz * dz);
}

static void test_swimming_and_walking_approaches(void) {
  static const Vec3 directions[] = {{1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f},
      {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f},
      {0.70710678f, 0.0f, 0.70710678f}, {-0.70710678f, 0.0f, 0.70710678f},
      {0.70710678f, 0.0f, -0.70710678f}, {-0.70710678f, 0.0f, -0.70710678f}};
  World world = {0};
  assert(create_terrain_streamed(&world, 1024, (Vec3){0}, 1.0f, 1));
  for (size_t i = 0; i < sizeof(directions) / sizeof(*directions); i++) {
    Player player = {.position = {256.0f + directions[i].x * 137.0f, 0.4f,
                                  256.0f + directions[i].z * 137.0f},
        .eyeHeight = 1.0f, .aim = {.front = vec3_scale(directions[i], -1.0f)}};
    for (int tick = 0; tick < 300; tick++)
      update_player(&player, (PlayerInput){.forward = 1.0f, .swimUp = 1}, GAME_TICK_SECONDS, &world);
    assert(player.grounded && fabsf(player.position.y - 1.0f) < 0.001f);
    assert(fabsf(player_radius(&player) - 128.0f) < 1.5f);
    assert(terrain_sample_column(1, (int)floorf(player.position.x),
                                  (int)floorf(player.position.z)).stoneTopY == 1);
    update_player(&player, (PlayerInput){.forward = 1.0f, .jump = 1}, GAME_TICK_SECONDS, &world);
    for (int tick = 0; tick < 300; tick++)
      update_player(&player, (PlayerInput){.forward = 1.0f}, GAME_TICK_SECONDS, &world);
    assert(player.grounded && fabsf(player.position.y - 2.0f) < 0.001f);
    /* The circular contour is sampled at voxel centers; the body can stop
       slightly inside or outside its ideal radius at a diagonal stair. */
    assert(fabsf(player_radius(&player) - 118.0f) < 1.5f);
    assert(terrain_sample_column(1, (int)floorf(player.position.x),
                                  (int)floorf(player.position.z)).stoneTopY == 2);
    update_player(&player, (PlayerInput){.forward = 1.0f, .jump = 1}, GAME_TICK_SECONDS, &world);
    for (int tick = 0; tick < 120; tick++)
      update_player(&player, (PlayerInput){.forward = 1.0f}, GAME_TICK_SECONDS, &world);
    assert(player.grounded && fabsf(player.position.y - 3.0f) < 0.001f);
    assert(player_radius(&player) < 118.0f);
  }
  world_destroy(&world);
}

static void test_beach_spawn(void) {
  Game game = {0};
  assert(game_init(&game, (GameConfig){1024, {0.0f, 0.0f, 0.0f}, 1.0f, 1}));
  assert(game.player.position.x == 170.0f && game.player.position.y == 2.0f && game.player.position.z == 170.0f);
  assert(game.player.grounded);
  assert(game.player.aim.yaw == -90.0f && game.tick == 0);
  assert(world_get_block(&game.world, 0, 0, 0) == BLOCK_TYPE_WATER);
  assert(world_get_block(&game.world, 170, 1, 170) == BLOCK_TYPE_SAND);
  assert(world_get_block(&game.world, 170, 2, 170) == BLOCK_TYPE_EMPTY);
  for (int tick = 0; tick < 60; tick++) game_step(&game, (GameCommand){.yaw = -90.0f});
  assert(game.player.grounded && fabsf(game.player.position.y - 2.0f) < 0.001f);
  game_step(&game, (GameCommand){.yaw = -90.0f, .movement.jump = 1});
  assert(!game.player.grounded && game.player.position.y > 2.0f);
  game_destroy(&game);
}

int main(void) {
  test_profile_samples();
  test_outline_and_boundaries();
  test_materialization_and_edits();
  test_swimming_and_walking_approaches();
  test_beach_spawn();
  puts("Experimental beach generation checks passed.");
  return 0;
}
