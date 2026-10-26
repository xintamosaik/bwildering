#include "../game_logic.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>

static void assert_near(float actual, float expected) {
  assert(fabsf(actual - expected) < 0.0001f);
}
static World single_block_world(void) {
  World world = {0};
  assert(create_platform(&world, 1, (Vec3){0}, 1, 1));
  return world; /* Move ownership to the caller. */
}
static Player make_player(Vec3 position, int grounded) {
  return (Player){.position = position,
                  .grounded = grounded,
                  .camera = {.front = {0, 0, -1}, .up = {0, 1, 0}}};
}
static void set_column(World *world, int gridX, int gridZ, int height, BlockType type) {
  for (int y = -32; y < 32; y++)
    assert(world_set_block(world, world->minX + gridX, y, world->minZ + gridZ,
                           y >= -1 && y < height ? type : BLOCK_TYPE_EMPTY));
}
static void test_vector_normalization(void) {
  Vec3 normalized = vec3_normalize((Vec3){3.0f, 0.0f, 4.0f});
  assert_near(normalized.x, 0.6f);
  assert_near(normalized.y, 0.0f);
  assert_near(normalized.z, 0.8f);

  Vec3 zero = vec3_normalize((Vec3){0.0f, 0.0f, 0.0f});
  assert_near(zero.x, 0.0f);
  assert_near(zero.y, 0.0f);
  assert_near(zero.z, 0.0f);
}

static void test_matrix_multiply_identity(void) {
  mat4 identity;
  mat4 original = {1.0f, 2.0f,  3.0f,  4.0f,  5.0f,  6.0f,  7.0f,  8.0f,
                   9.0f, 10.0f, 11.0f, 12.0f, 13.0f, 14.0f, 15.0f, 16.0f};
  mat4 result;
  mat4_identity(identity);

  mat4_multiply(identity, original, result);
  for (int i = 0; i < 16; i++)
    assert_near(result[i], original[i]);

  mat4_multiply(original, identity, result);
  for (int i = 0; i < 16; i++)
    assert_near(result[i], original[i]);
}

static void test_block_type_palette(void) {
  Vec3 colors[BLOCK_TYPE_COUNT];
  const Vec3 expectedColors[BLOCK_TYPE_COUNT] = {
      [BLOCK_TYPE_GRASS] = {.x = 0.46f, .y = 0.52f, .z = 0.38f},
      [BLOCK_TYPE_STONE] = {.x = 0.53f, .y = 0.51f, .z = 0.48f},
      [BLOCK_TYPE_WATER] = {.x = 0.36f, .y = 0.50f, .z = 0.52f},
      [BLOCK_TYPE_SAND] = {.x = 0.68f, .y = 0.60f, .z = 0.44f},
  };
  for (int type = BLOCK_TYPE_GRASS; type < BLOCK_TYPE_COUNT; type++) {
    assert(block_type_color((BlockType)type, &colors[type]));
    assert_near(colors[type].x, expectedColors[type].x);
    assert_near(colors[type].y, expectedColors[type].y);
    assert_near(colors[type].z, expectedColors[type].z);
    assert(colors[type].x >= 0.0f && colors[type].x <= 1.0f);
    assert(colors[type].y >= 0.0f && colors[type].y <= 1.0f);
    assert(colors[type].z >= 0.0f && colors[type].z <= 1.0f);
  }

  for (int first = 0; first < BLOCK_TYPE_COUNT; first++) {
    for (int second = first + 1; second < BLOCK_TYPE_COUNT; second++) {
      assert(colors[first].x != colors[second].x || colors[first].y != colors[second].y ||
             colors[first].z != colors[second].z);
    }
  }

  Vec3 color = {9.0f, 9.0f, 9.0f};
  assert(!block_type_color((BlockType)-1, &color));
  assert(!block_type_color(BLOCK_TYPE_COUNT, &color));
  assert(!block_type_color(BLOCK_TYPE_GRASS, NULL));
  assert_near(color.x, 9.0f);
}

static void test_diagonal_movement_is_normalized(void) {
  World world = {0};
  Player forward = make_player((Vec3){0.0f, 10.0f, 0.0f}, 0);
  Player diagonal = forward;
  update_player(&forward, (PlayerInput){.forward = 1.0f}, 0.25f, &world);
  update_player(&diagonal, (PlayerInput){.forward = 1.0f, .strafe = 1.0f}, 0.25f, &world);

  float forwardDistance = fabsf(forward.position.z);
  float diagonalDistance = hypotf(diagonal.position.x, diagonal.position.z);
  assert_near(forwardDistance, 1.0f);
  assert_near(diagonalDistance, forwardDistance);
  world_destroy(&world);
}

static void test_landing_on_block(void) {
  World world = single_block_world();
  Player player = make_player((Vec3){0.0f, 0.1f, 0.0f}, 0);
  player.velocityY = -1.0f;

  update_player(&player, (PlayerInput){0}, 0.1f, &world);

  assert(player.grounded);
  assert_near(player.position.y, 0.0f);
  assert_near(player.velocityY, 0.0f);
  world_destroy(&world);
}

static void test_falling_off_platform_edge(void) {
  World world = single_block_world();
  Player player = make_player((Vec3){0.0f, 0.0f, 0.0f}, 1);

  update_player(&player, (PlayerInput){.forward = 1.0f}, 0.2f, &world);

  assert(!player.grounded);
  assert(player.position.z < -0.5f);
  assert(player.position.y < 0.0f);
  assert(player.velocityY < 0.0f);
  world_destroy(&world);
}

static void test_jump_requires_grounding(void) {
  World world = single_block_world();
  Player grounded = make_player((Vec3){0.0f, 0.0f, 0.0f}, 1);
  Player airborne = make_player((Vec3){0.0f, 2.0f, 0.0f}, 0);
  Player withoutJump = airborne;

  update_player(&grounded, (PlayerInput){.jump = 1}, 0.1f, &world);
  update_player(&airborne, (PlayerInput){.jump = 1}, 0.1f, &world);
  update_player(&withoutJump, (PlayerInput){0}, 0.1f, &world);

  assert(!grounded.grounded);
  assert(grounded.velocityY > 0.0f);
  assert(grounded.position.y > 0.0f);
  assert(!airborne.grounded);
  assert(airborne.velocityY < 0.0f);
  assert_near(airborne.velocityY, withoutJump.velocityY);
  assert_near(airborne.position.y, withoutJump.position.y);
  world_destroy(&world);
}

static void test_jump_height_and_timing(void) {
  World world = single_block_world();
  const int rates[] = {60, 120, 240};
  for (size_t rate = 0; rate < sizeof(rates) / sizeof(rates[0]); rate++) {
    Player player = make_player((Vec3){0}, 1);
    const float dt = 1.0f / rates[rate];
    float peak = 0.0f;
    float timeToPeak = 0.0f;
    int frame = 0;
    do {
      update_player(&player, (PlayerInput){.jump = frame == 0}, dt, &world);
      frame++;
      if (player.position.y > peak) {
        peak = player.position.y;
        timeToPeak = frame * dt;
      }
    } while (!player.grounded && frame < rates[rate]);

    const float airtime = frame * dt;
    /* Preserve one-block clearance without lingering in the air. */
    assert(peak > 1.15f && peak < 1.35f);
    assert(timeToPeak > 0.3f && timeToPeak < 0.4f);
    assert(airtime > 0.6f && airtime < 0.7f);
    assert(airtime - timeToPeak < timeToPeak);
    assert(player.grounded);
    assert_near(player.position.y, 0.0f);
    assert_near(player.velocityY, 0.0f);
  }
  world_destroy(&world);
}

static void test_random_bounded_output_range(void) {
  uint32_t state = 0x12345678u;
  assert(random_bounded(&state, 0u) == 0u);

  for (uint32_t upperBound = 1; upperBound <= 17; upperBound++) {
    uint32_t min = UINT32_MAX;
    uint32_t max = 0u;
    for (int i = 0; i < 20000; i++) {
      state = (state * 1103515245u + 12345u) & UINT32_MAX;
      uint32_t value = random_bounded(&state, upperBound);
      assert(value < upperBound);
      if (value < min)
        min = value;
      if (value > max)
        max = value;
    }
    assert(min == 0u);
    assert(max == upperBound - 1u || upperBound == 1u);
  }
}

static void test_terrain_collision(void) {
  World world = {0};
  assert(create_platform(&world, 3, (Vec3){0}, 1, 1));
  /* Raised column directly to the right of the center cell. */
  set_column(&world, 2, 1, 1, BLOCK_TYPE_STONE);
  assert_near(world_surface_height(&world, 1, 0), 1);
  assert(isnan(world_surface_height(&world, 2, 0)));
  assert(isnan(world_surface_height(&world, NAN, 0)));
  assert(isnan(world_surface_height(NULL, 0, 0)));

  Player player = make_player((Vec3){0, 0, 0}, 1);
  update_player(&player, (PlayerInput){.strafe = 1}, 0.25f, &world);
  assert(player.position.x < 0.5f);
  assert(player.grounded);
  assert_near(player.position.y, 0);
  /* A long movement cannot skip the raised column. */
  player = make_player((Vec3){0, 0, 0}, 1);
  update_player(&player, (PlayerInput){.strafe = 1}, 2, &world);
  assert(player.position.x < 0.5f);
  /* Jumping over a one-block step lands on its top. */
  player = make_player((Vec3){0.25f, 0, 0}, 1);
  update_player(&player, (PlayerInput){.jump = 1}, 0.01f, &world);
  for (int i = 0; i < 45; i++)
    update_player(&player, (PlayerInput){.strafe = 1}, 0.01f, &world);
  assert(player.position.x > 0.5f);
  for (int i = 0; i < 100; i++)
    update_player(&player, (PlayerInput){0}, 0.01f, &world);
  assert(player.grounded);
  assert_near(player.position.y, 1);
  /* Walking down begins falling and lands on the lower neighbor. */
  player = make_player((Vec3){1, 1, 0}, 1);
  int beganFalling = 0;
  for (int i = 0; i < 25; i++) {
    update_player(&player, (PlayerInput){.strafe = -1}, 0.01f, &world);
    if (!player.grounded)
      beganFalling = 1;
  }
  assert(beganFalling);
  for (int i = 0; i < 100; i++)
    update_player(&player, (PlayerInput){0}, 0.01f, &world);
  assert(player.grounded);
  assert_near(player.position.y, 0);
  world_destroy(&world);
}

static void test_camera_stays_clear_of_walls(void) {
  const int dx[] = {-1, 1, 0, 0};
  const int dz[] = {0, 0, -1, 1};
  for (int direction = 0; direction < 4; direction++) {
    World world = {0};
    assert(create_platform(&world, 3, (Vec3){0}, 1, 1));
    set_column(&world, 1 + dx[direction], 1 + dz[direction], 3, BLOCK_TYPE_STONE);
    Player player = make_player((Vec3){0, 0, 0}, 1);
    player.eyeHeight = 1;
    const PlayerInput input = {.strafe = dx[direction], .forward = -dz[direction]};
    for (int frame = 0; frame < 100; frame++)
      update_player(&player, input, 0.12f, &world);
    const float approach =
        player.position.x * dx[direction] + player.position.z * dz[direction];
    assert(approach > 0.1f && approach <= 0.25f);
    assert(player.grounded);
    assert_near(player.position.y, 0);
    world_destroy(&world);
  }
}

static void test_player_footprint_at_corners_and_ledges(void) {
  World world = {0};
  assert(create_platform(&world, 3, (Vec3){0}, 1, 1));
  /* The center point misses this column; the player's corner overlaps it. */
  set_column(&world, 2, 2, 3, BLOCK_TYPE_STONE);
  Player player = make_player((Vec3){0, 0, 0.6f}, 1);
  update_player(&player, (PlayerInput){.strafe = 1}, 0.12f, &world);
  assert(player.position.x <= 0.25f);
  /* Tangential movement still slides along the wall. */
  const float previousX = player.position.x;
  update_player(&player, (PlayerInput){.strafe = 1, .forward = -1}, 0.12f, &world);
  assert_near(player.position.x, previousX);
  assert(player.position.z > 0.6f);

  world_destroy(&world);
  world = single_block_world();
  player = make_player((Vec3){0.6f, 0, 0}, 1);
  update_player(&player, (PlayerInput){0}, 0.01f, &world);
  assert(player.grounded); /* Footprint still overlaps the ledge. */
  player = make_player((Vec3){0.6f, 0.1f, 0}, 0);
  player.velocityY = -1;
  update_player(&player, (PlayerInput){0}, 0.1f, &world);
  assert(player.grounded);
  assert_near(player.position.y, 0);
  update_player(&player, (PlayerInput){.strafe = 1}, 0.1f, &world);
  assert(!player.grounded);
  world_destroy(&world);
}

static void clear_dirty(World *world) {
  for (size_t c = 0; c < world->chunkCount; c++)
    for (int s = 0; s < WORLD_SECTION_COUNT; s++)
      if (world->chunks[c].sections[s])
        world->chunks[c].sections[s]->dirty = 0;
}

static int dirty_count(const World *world) {
  int count = 0;
  for (size_t c = 0; c < world->chunkCount; c++)
    for (int s = 0; s < WORLD_SECTION_COUNT; s++)
      if (world->chunks[c].sections[s])
        count += world->chunks[c].sections[s]->dirty != 0;
  return count;
}

static void test_coordinates_and_storage(void) {
  const int coordinates[] = {INT_MIN, -17, -16, -1, 0, 15, 16, INT_MAX};
  for (size_t i = 0; i < sizeof(coordinates) / sizeof(*coordinates); i++) {
    int v = coordinates[i], local = voxel_local_coord(v),
        section = voxel_section_coord(v);
    assert(local >= 0 && local < 16);
    assert((int64_t)section * 16 + local == v);
  }
  World world = {0};
  assert(world_init(&world, 48, (Vec3){12, -3, 8}, 2));
  assert(world.voxelCount == 0 && world.sectionCount == 0);
  assert(world_get_block(&world, 0, 0, 0) == BLOCK_TYPE_EMPTY);
  assert(world_set_block(&world, 0, 0, 0, BLOCK_TYPE_EMPTY));
  assert(world.sectionCount == 0);
  for (int x = -17; x <= 16; x++)
    for (int y = -17; y <= 16; y++)
      for (int z = -17; z <= 16; z++)
        assert(world_set_block(&world, x, y, z,
                               (BlockType)((voxel_local_coord(x) + voxel_local_coord(y) +
                                            voxel_local_coord(z)) %
                                           4)));
  assert(world.voxelCount == 34 * 34 * 34 && world.sectionCount == 64);
  for (int x = -17; x <= 16; x++)
    for (int y = -17; y <= 16; y++)
      for (int z = -17; z <= 16; z++)
        assert(world_get_block(&world, x, y, z) ==
               (BlockType)((voxel_local_coord(x) + voxel_local_coord(y) +
                            voxel_local_coord(z)) %
                           4));
  size_t count = world.voxelCount;
  assert(!world_set_block(&world, INT_MAX, 0, 0, BLOCK_TYPE_GRASS));
  assert(!world_set_block(&world, 0, 32, 0, BLOCK_TYPE_GRASS));
  assert(!world_set_block(&world, 0, -33, 0, BLOCK_TYPE_GRASS));
  assert(!world_set_block(&world, 0, 0, 0, (BlockType)-1));
  assert(!world_set_block(&world, 0, 0, 0, BLOCK_TYPE_COUNT));
  assert(world.voxelCount == count);
  Chunk *original = world.chunks;
  assert(!world_init(&world, 0, (Vec3){0}, 1));
  assert(!create_terrain(&world, 193, (Vec3){0}, 1, 1));
  assert(!create_platform(&world, 1, (Vec3){NAN, 0, 0}, 1, 1));
  assert(!world_init(&world, 1, (Vec3){0}, 0));
  assert(!world_init(&world, 1, (Vec3){0}, NAN));
  assert(!world_init(&world, 1, (Vec3){0}, 0.0001f));
  assert(!world_init(&world, 1, (Vec3){1e10f,0,0}, 0.001f));
  assert(world.chunks == original && world.voxelCount == count);
  assert(world_get_block(&world, INT_MIN, INT_MAX, INT_MIN) == BLOCK_TYPE_EMPTY);
  world_destroy(&world);
  world_destroy(&world);
  world_destroy(NULL);
  assert(!world.chunks && !world.voxelCount);
}

static size_t mesh_world(const World *world, size_t *draws, size_t *nonempty) {
  size_t vertices = 0;
  *draws = 0;
  *nonempty = 0;
  for (size_t c = 0; c < world->chunkCount; c++)
    for (int s = 0; s < WORLD_SECTION_COUNT; s++) {
      SectionMesh mesh = {0};
      assert(section_mesh_build(world, world->chunks[c].x, WORLD_MIN_SECTION_Y + s,
                                world->chunks[c].z, &mesh));
      vertices += mesh.vertexCount;
      *nonempty += mesh.vertexCount != 0;
      size_t end = 0;
      for (int t = 0; t < BLOCK_TYPE_COUNT; t++) {
        assert(mesh.firstVertex[t] == end);
        assert(mesh.typeVertexCount[t] % 6 == 0);
        end += mesh.typeVertexCount[t];
        *draws += mesh.typeVertexCount[t] != 0;
      }
      assert(end == mesh.vertexCount);
      section_mesh_destroy(&mesh);
    }
  return vertices;
}

static uint64_t hash_bytes(const uint8_t *values, size_t count) {
  uint64_t hash = UINT64_C(14695981039346656037);
  for (size_t i = 0; i < count; i++)
    hash = (hash ^ values[i]) * UINT64_C(1099511628211);
  return hash;
}

static void test_generator_regressions(void) {
  World world = {0};
  const Vec3 center = {12, -3, 8};
  uint8_t materials[192 * 192], heights[192 * 192];
  for (int terrain = 0; terrain < 2; terrain++) {
    if (terrain)
      assert(create_terrain(&world, 192, center, 2, UINT32_C(0x4B1D2A37)));
    else
      assert(create_platform(&world, 192, center, 2, UINT32_C(0x4B1D2A37)));
    assert(world.chunkCount == 144);
    size_t voxels = 0, expectedQuads = 0;
    for (int x = 0; x < 192; x++)
      for (int z = 0; z < 192; z++) {
        int index = x * 192 + z;
        float top = world_surface_height(&world, center.x + (x - 96 + 0.5f) * 2,
                                         center.z + (z - 96 + 0.5f) * 2);
        int height = (int)roundf((top - center.y) / 2);
        assert(height >= 0 && height <= 15);
        heights[index] = (uint8_t)height;
        materials[index] = (uint8_t)world_get_block(&world, x - 96, height - 1, z - 96);
        for (int y = -1; y < height; y++)
          assert(world_get_block(&world, x - 96, y, z - 96) == materials[index]);
        assert(world_get_block(&world, x - 96, height, z - 96) == BLOCK_TYPE_EMPTY);
        voxels += (size_t)height + 1;
      }
    assert(hash_bytes(materials, sizeof(materials)) ==
           (terrain ? UINT64_C(10456764649290648415) : UINT64_C(9772508476359371607)));
    assert(hash_bytes(heights, sizeof(heights)) ==
           (terrain ? UINT64_C(1177817274686891020) : UINT64_C(13722806926226842405)));
    assert(world.voxelCount == voxels);
    const int dx[] = {-1, 1, 0, 0}, dz[] = {0, 0, -1, 1};
    for (int x = 0; x < 192; x++)
      for (int z = 0; z < 192; z++) {
        int height = heights[x * 192 + z];
        expectedQuads += 2; /* Top and bottom. */
        for (int d = 0; d < 4; d++) {
          int nx = x + dx[d], nz = z + dz[d];
          int neighbor =
              nx < 0 || nx >= 192 || nz < 0 || nz >= 192 ? -1 : heights[nx * 192 + nz];
          if (height > neighbor)
            expectedQuads += (size_t)(height - neighbor);
        }
      }
    size_t draws, sections;
    clock_t start = clock();
    size_t vertices = mesh_world(&world, &draws, &sections);
    double ms = 1000.0 * (clock() - start) / CLOCKS_PER_SEC;
    assert(vertices == expectedQuads * 6);
    if (!terrain) {
      assert(sections == 144);
      assert(vertices == (2 * 192 * 192 + 4 * 192) * 6);
    }
    printf("%s: %zu populated meshes, %zu vertices, %zu bytes, %zu draws, CPU %.3f ms\n",
           terrain ? "terrain" : "flat", sections, vertices,
           vertices * sizeof(SurfaceVertex), draws, ms);
  }
  /* Global shuffle balance, zero seed, and odd/small transformed worlds. */
  assert(create_platform(&world, 5, center, 2, 0));
  World repeated = {0};
  assert(create_platform(&repeated, 5, center, 2, 0));
  int counts[4] = {0};
  for (int x = 0; x < 5; x++)
    for (int z = 0; z < 5; z++) {
      BlockType type = world_get_block(&world, world.minX + x, -1, world.minZ + z);
      counts[type]++;
      assert(type ==
             world_get_block(&repeated, repeated.minX + x, -1, repeated.minZ + z));
      assert_near(
          world_surface_height(&world, center.x + (x - 2) * 2, center.z + (z - 2) * 2),
          center.y);
    }
  assert(counts[0] == 7 && counts[1] == 6 && counts[2] == 6 && counts[3] == 6);
  assert(create_platform(&world, 1, center, 2, 0));
  assert_near(world.origin.x, 11);
  assert_near(world_surface_height(&world, 12, 8), -3);
  assert(isnan(world_surface_height(&world, NAN, 8)));
  assert(isnan(world_surface_height(&world, 14, 8)));
  world_destroy(&world);
  world_destroy(&repeated);
}

static void test_section_faces_and_replacement(void) {
  World world = {0};
  assert(world_init(&world, 48, (Vec3){10, 4, -6}, 2));
  const int delta[6][3] = {{-1, 0, 0}, {1, 0, 0},  {0, -1, 0},
                           {0, 1, 0},  {0, 0, -1}, {0, 0, 1}};
  for (int d = 0; d < 6; d++) {
    /* Each pair straddles a section boundary, including negative coordinates. */
    int a[3] = {-1, -1, -1}, b[3] = {-1, -1, -1};
    int axis = d / 2;
    a[axis] = d % 2 ? -1 : 0;
    for (int j = 0; j < 3; j++)
      b[j] = a[j] + delta[d][j];
    assert(world_set_block(&world, a[0], a[1], a[2], BLOCK_TYPE_STONE));
    SectionMesh one = {0};
    assert(section_mesh_build(&world, voxel_section_coord(a[0]),
                              voxel_section_coord(a[1]), voxel_section_coord(a[2]),
                              &one));
    assert(one.vertexCount == 36);
    /* Face normals point away from this voxel's center. */
    Vec3 center = {world.origin.x + (a[0] + 0.5f) * 2, world.origin.y + (a[1] + 0.5f) * 2,
                   world.origin.z + (a[2] + 0.5f) * 2};
    for (size_t i = 0; i < one.vertexCount; i += 6) {
      SurfaceVertex *v = &one.vertices[i];
      Vec3 first = {v[0].x, v[0].y, v[0].z};
      Vec3 edge1 = {v[1].x - v[0].x, v[1].y - v[0].y, v[1].z - v[0].z};
      Vec3 edge2 = {v[2].x - v[0].x, v[2].y - v[0].y, v[2].z - v[0].z};
      assert(vec3_dot(vec3_cross(edge1, edge2), vec3_sub(first, center)) > 0);
    }
    SurfaceVertex *original = one.vertices;
    assert(!section_mesh_build(&world, INT_MAX, 0, 0, &one));
    assert(one.vertices == original && one.vertexCount == 36);
    assert(world_set_block(&world, b[0], b[1], b[2], BLOCK_TYPE_WATER));
    size_t draws, nonempty;
    assert(mesh_world(&world, &draws, &nonempty) == 60);
    assert(world_set_block(&world, a[0], a[1], a[2], BLOCK_TYPE_EMPTY));
    assert(world_set_block(&world, b[0], b[1], b[2], BLOCK_TYPE_EMPTY));
    assert(section_mesh_build(&world, voxel_section_coord(a[0]),
                              voxel_section_coord(a[1]), voxel_section_coord(a[2]),
                              &one));
    assert(one.vertices == NULL && one.vertexCount == 0);
    section_mesh_destroy(&one);
  }
  /* A sealed 3x3x3 shell has 54 outer faces and six cavity faces. */
  for (int x = 1; x <= 3; x++)
    for (int y = 1; y <= 3; y++)
      for (int z = 1; z <= 3; z++)
        if (x != 2 || y != 2 || z != 2)
          assert(world_set_block(&world, x, y, z, BLOCK_TYPE_GRASS));
  size_t draws, nonempty;
  assert(mesh_world(&world, &draws, &nonempty) == 60 * 6);
  world_destroy(&world);
}

static void test_dirty_neighbors(void) {
  World world = {0};
  assert(world_init(&world, 48, (Vec3){0}, 1));
  /* Allocate all 27 sections surrounding the origin section. */
  for (int x = -1; x <= 1; x++)
    for (int y = -1; y <= 1; y++)
      for (int z = -1; z <= 1; z++)
        assert(world_set_block(&world, x * 16 + 1, y * 16 + 1, z * 16 + 1,
                               BLOCK_TYPE_STONE));
  const int positions[][3] = {{2, 2, 2}, {0, 2, 2},    {0, 0, 2},
                              {0, 0, 0}, {15, 15, 15}, {-1, -1, -1}};
  const int expected[] = {1, 2, 3, 4, 4, 4};
  for (int i = 0; i < 6; i++) {
    clear_dirty(&world);
    int x = positions[i][0], y = positions[i][1], z = positions[i][2];
    assert(world_set_block(&world, x, y, z, BLOCK_TYPE_GRASS));
    assert(dirty_count(&world) == expected[i]);
    clear_dirty(&world);
    assert(world_set_block(&world, x, y, z, BLOCK_TYPE_GRASS));
    assert(dirty_count(&world) == 0);
    assert(world_set_block(&world, x, y, z, BLOCK_TYPE_EMPTY));
    assert(dirty_count(&world) == expected[i]);
  }
  const int faces[][3] = {{0, 2, 2},  {15, 2, 2}, {2, 0, 2},
                          {2, 15, 2}, {2, 2, 0},  {2, 2, 15}};
  for (int i = 0; i < 6; i++) {
    clear_dirty(&world);
    assert(
        world_set_block(&world, faces[i][0], faces[i][1], faces[i][2], BLOCK_TYPE_WATER));
    assert(dirty_count(&world) == 2);
  }
  world_destroy(&world);
}

static void test_cave_and_ceiling_collision(void) {
  World world = {0};
  assert(world_init(&world, 32, (Vec3){0}, 1));
  for (int x = -3; x <= 3; x++)
    for (int z = -3; z <= 3; z++) {
      assert(world_set_block(&world, x, -1, z, BLOCK_TYPE_STONE));
      assert(world_set_block(&world, x, 3, z, BLOCK_TYPE_STONE));
    }
  Player player = make_player((Vec3){0.5f, 0, 0.5f}, 1);
  /* Floor landing inside the cave must ignore its roof. */
  player.position.y = 0.8f;
  player.grounded = 0;
  player.velocityY = -1;
  for (int i = 0; i < 100; i++)
    update_player(&player, (PlayerInput){0}, 0.01f, &world);
  assert(player.grounded);
  assert_near(player.position.y, 0);
  assert_near(world_surface_height(&world, 0.5f, 0.5f), 4);
  /* Lower roof: jumping strikes it and clears upward velocity. */
  for (int x = -3; x <= 3; x++)
    for (int z = -3; z <= 3; z++)
      assert(world_set_block(&world, x, 2, z, BLOCK_TYPE_STONE));
  int hitCeiling = 0;
  for (int i = 0; i < 100; i++) {
    update_player(&player, (PlayerInput){.jump = i == 0}, 0.01f, &world);
    assert(player.position.y <= 0.5001f);
    if (player.position.y > 0.49f && player.velocityY == 0)
      hitCeiling = 1;
  }
  assert(hitCeiling && player.grounded);
  /* Walk under an overhang and across x/z=0 chunk boundaries. */
  update_player(&player, (PlayerInput){.strafe = -1}, 0.4f, &world);
  assert(player.position.x < 0 && player.grounded);
  update_player(&player, (PlayerInput){.forward = 1}, 0.4f, &world);
  assert(player.position.z < 0 && player.grounded);
  /* A large falling movement cannot tunnel through the floor. */
  player = make_player((Vec3){0.5f, 1, 0.5f}, 0);
  /* Remove the low roof before this fall. */
  for (int x = -3; x <= 3; x++)
    for (int z = -3; z <= 3; z++)
      assert(world_set_block(&world, x, 2, z, BLOCK_TYPE_EMPTY));
  player.velocityY = -100;
  update_player(&player, (PlayerInput){0}, 0.25f, &world);
  assert(player.grounded);
  assert_near(player.position.y, 0);
  assert(world_set_block(&world, 0, -1, 0, BLOCK_TYPE_EMPTY));
  update_player(&player, (PlayerInput){0}, 0.1f, &world);
  assert(!player.grounded && player.position.y < 0);
  /* Landing crosses the Y=0 section boundary; lower floors also work. */
  assert(world_set_block(&world, 0, -17, 0, BLOCK_TYPE_STONE));
  player = make_player((Vec3){0.5f, -14, 0.5f}, 0);
  player.velocityY = -10;
  for (int i = 0; i < 100; i++)
    update_player(&player, (PlayerInput){0}, 0.01f, &world);
  assert(player.grounded);
  assert_near(player.position.y, -16);
  world_destroy(&world);
}

int main(void) {
  test_vector_normalization();
  test_matrix_multiply_identity();
  test_block_type_palette();
  test_diagonal_movement_is_normalized();
  test_landing_on_block();
  test_falling_off_platform_edge();
  test_jump_requires_grounding();
  test_jump_height_and_timing();
  test_random_bounded_output_range();
  test_terrain_collision();
  test_camera_stays_clear_of_walls();
  test_player_footprint_at_corners_and_ledges();
  test_coordinates_and_storage();
  test_generator_regressions();
  test_section_faces_and_replacement();
  test_dirty_neighbors();
  test_cave_and_ceiling_collision();
  puts("All game logic tests passed.");
  return 0;
}
