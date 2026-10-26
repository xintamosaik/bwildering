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
  for (int type = BLOCK_TYPE_GRASS; type < BLOCK_TYPE_COUNT; type++) {
    assert(block_type_color((BlockType)type, &colors[type]));
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
  assert_near(color.y, 9.0f);
  assert_near(color.z, 9.0f);
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

static void test_running_and_crouching_speed(void) {
  World world = {0};
  const PlayerInput directions[] = {
      {.forward = 1}, {.forward = -1}, {.strafe = 1}, {.forward = 1, .strafe = 1}};
  for (size_t d = 0; d < sizeof(directions) / sizeof(*directions); d++) {
    float distances[4];
    for (int mode = 0; mode < 4; mode++) {
      Player player = make_player((Vec3){0, 10, 0}, 0);
      PlayerInput input = directions[d];
      input.run = mode == 1 || mode == 3;
      input.crouch = mode == 2 || mode == 3;
      update_player(&player, input, 0.25f, &world);
      distances[mode] = hypotf(player.position.x, player.position.z);
      /* Releasing the modifier restores ordinary movement immediately. */
      Vec3 previous = player.position;
      update_player(&player, directions[d], 0.25f, &world);
      assert_near(hypotf(player.position.x - previous.x, player.position.z - previous.z), 1);
    }
    assert_near(distances[0], 1);
    assert_near(distances[1], distances[0] * 1.5f);
    assert_near(distances[2], distances[0] * 0.75f);
    assert_near(distances[3], distances[2]); /* Crouch takes priority over running. */
  }
}

static void test_crouching_lowers_view_until_released(void) {
  World world = single_block_world();
  Player player = make_player((Vec3){0}, 1);
  player.eyeHeight = 1;
  Vec3 standingEye = player_eye_position(&player);
  for (int cycle = 0; cycle < 2; cycle++) {
    for (int frame = 0; frame < 120; frame++) {
      update_player(&player, (PlayerInput){.crouch = 1}, 1.0f / 60, &world);
      Vec3 eye = player_eye_position(&player);
      assert_near(eye.x, standingEye.x);
      assert_near(eye.y, standingEye.y - 0.3f);
      assert_near(eye.z, standingEye.z);
      assert_near(player.position.y, 0);
      assert(player.grounded);
    }
    update_player(&player, (PlayerInput){0}, 1.0f / 60, &world);
    assert_near(player_eye_position(&player).y, standingEye.y);
  }
  /* The lowered view follows the player when jumping, too. */
  update_player(&player, (PlayerInput){.crouch = 1, .jump = 1}, 1.0f / 60, &world);
  assert(player.position.y > 0);
  assert_near(player_eye_position(&player).y - player.position.y, 0.7f);
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

static void assert_dirty_sections(const World *world, const int expected[][3], size_t count) {
  for (size_t c = 0; c < world->chunkCount; c++)
    for (int s = 0; s < WORLD_SECTION_COUNT; s++) {
      const Chunk *chunk = &world->chunks[c];
      int shouldBeDirty = 0;
      for (size_t e = 0; e < count; e++)
        shouldBeDirty |= chunk->x == expected[e][0] &&
                         WORLD_MIN_SECTION_Y + s == expected[e][1] &&
                         chunk->z == expected[e][2];
      assert((chunk->sections[s] && chunk->sections[s]->dirty) == shouldBeDirty);
    }
}

static void test_world_size_contract(void) {
  const int accepted[] = {1, WORLD_MAX_BLOCKS_PER_SIDE};
  for (size_t i = 0; i < sizeof(accepted) / sizeof(*accepted); i++) {
    World world = {0};
    int side = accepted[i];
    assert(world_init(&world, side, (Vec3){0}, 1));
    int first = -(side / 2), last = first + side - 1;
    assert(world_set_block(&world, first, 0, first, BLOCK_TYPE_STONE));
    assert(world_set_block(&world, last, 0, last, BLOCK_TYPE_STONE));
    assert(world_get_block(&world, first, 0, first) == BLOCK_TYPE_STONE);
    assert(world_get_block(&world, last, 0, last) == BLOCK_TYPE_STONE);
    assert(!world_set_block(&world, first - 1, 0, first, BLOCK_TYPE_GRASS));
    assert(!world_set_block(&world, last + 1, 0, last, BLOCK_TYPE_GRASS));
    world_destroy(&world);
    assert(world_init_region(&world, -7, 11, side, (Vec3){0}, 1));
    assert(world_set_block(&world, -7 + side - 1, 0, 11 + side - 1, BLOCK_TYPE_SAND));
    assert(world_get_block(&world, -7 + side - 1, 0, 11 + side - 1) == BLOCK_TYPE_SAND);
    world_destroy(&world);
  }
  const int rejected[] = {-1, 0, WORLD_MAX_BLOCKS_PER_SIDE + 1};
  World sentinel = single_block_world();
  assert(world_set_block(&sentinel, 0, -1, 0, BLOCK_TYPE_STONE));
  for (size_t i = 0; i < sizeof(rejected) / sizeof(*rejected); i++)
    for (int operation = 0; operation < 5; operation++) {
      int side = rejected[i], result;
      switch (operation) {
      case 0: result = world_init(&sentinel, side, (Vec3){0}, 1); break;
      case 1: result = world_init_region(&sentinel, -7, 11, side, (Vec3){0}, 1); break;
      case 2: result = create_platform(&sentinel, side, (Vec3){0}, 1, 1); break;
      case 3: result = create_terrain(&sentinel, side, (Vec3){0}, 1, 1); break;
      default: result = create_terrain_region(&sentinel, -7, 11, side, (Vec3){0}, 1, 1); break;
      }
      assert(!result);
      assert(sentinel.blocksPerSide == 1);
      assert(world_get_block(&sentinel, 0, -1, 0) == BLOCK_TYPE_STONE);
      assert_near(world_surface_height(&sentinel, 0, 0), 0);
    }
  world_destroy(&sentinel);
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
                                           BLOCK_TYPE_COUNT)));
  assert(world.voxelCount == 34 * 34 * 34 && world.sectionCount == 64);
  for (int x = -17; x <= 16; x++)
    for (int y = -17; y <= 16; y++)
      for (int z = -17; z <= 16; z++)
        assert(world_get_block(&world, x, y, z) ==
               (BlockType)((voxel_local_coord(x) + voxel_local_coord(y) +
                            voxel_local_coord(z)) %
                           BLOCK_TYPE_COUNT));
  size_t count = world.voxelCount;
  assert(!world_set_block(&world, INT_MAX, 0, 0, BLOCK_TYPE_GRASS));
  assert(!world_set_block(&world, 0, 32, 0, BLOCK_TYPE_GRASS));
  assert(!world_set_block(&world, 0, -33, 0, BLOCK_TYPE_GRASS));
  assert(!world_set_block(&world, 0, 0, 0, (BlockType)-1));
  assert(!world_set_block(&world, 0, 0, 0, BLOCK_TYPE_COUNT));
  assert(world.voxelCount == count);
  Chunk *original = world.chunks;
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

static double world_mesh_surface_area(const World *world) {
  double area = 0;
  for (size_t c = 0; c < world->chunkCount; c++)
    for (int s = 0; s < WORLD_SECTION_COUNT; s++) {
      SectionMesh mesh = {0};
      assert(section_mesh_build(world, world->chunks[c].x, WORLD_MIN_SECTION_Y + s,
                                world->chunks[c].z, &mesh));
      assert(mesh.vertexCount % 3 == 0);
      for (size_t i = 0; i < mesh.vertexCount; i += 3) {
        SurfaceVertex a = mesh.vertices[i], b = mesh.vertices[i + 1], d = mesh.vertices[i + 2];
        Vec3 cross = vec3_cross((Vec3){b.x - a.x, b.y - a.y, b.z - a.z},
                                (Vec3){d.x - a.x, d.y - a.y, d.z - a.z});
        area += 0.5 * sqrt((double)cross.x * cross.x + (double)cross.y * cross.y +
                           (double)cross.z * cross.z);
      }
      section_mesh_destroy(&mesh);
    }
  return area;
}

static void test_signed_terrain_sampling(void) {
  for (int scale = 8; scale <= 32; scale *= 2) {
    int seedChanges = 0;
    int values[] = {INT_MIN, WORLD_COORD_MIN, -scale - 1, -scale, -1, 0,
                    scale - 1, scale, scale + 1, WORLD_COORD_MAX, INT_MAX};
    for (size_t i = 0; i < sizeof(values) / sizeof(*values); i++) {
      int x = values[i], q = voxel_floor_div(x, scale), r = voxel_floor_mod(x, scale);
      assert((int64_t)q * scale + r == x && r >= 0 && r < scale);
      float fraction = (float)r / scale;
      assert(fraction >= 0 && fraction < 1);
      for (size_t j = 0; j < sizeof(values) / sizeof(*values); j++)
        for (uint32_t seed = 0; seed < 3; seed++) {
          float actual = terrain_noise(x, values[j], scale, seed);
          assert(actual >= 0 && actual <= 1);
          assert(actual == terrain_noise(x, values[j], scale, seed));
          seedChanges += actual != terrain_noise(x, values[j], scale, seed + 1);
        }
    }
    assert(seedChanges > 0);
    /* Smoothness across signed coordinates and lattice seams is observable. */
    for (int x = -65; x <= 65; x++)
      for (int z = -65; z <= 65; z++) {
        float n = terrain_noise(x, z, scale, 123);
        assert(fabsf(n - terrain_noise(x + 1, z, scale, 123)) <= 1.5f / scale);
        assert(fabsf(n - terrain_noise(x, z + 1, scale, 123)) <= 1.5f / scale);
        /* Scaling coordinates and wavelength together preserves a sample. */
        if (scale < 32)
          assert_near(n, terrain_noise(x * 2, z * 2, scale * 2, 123));
      }
  }
  assert(isnan(terrain_noise(0, 0, 0, 1)));
  assert(isnan(terrain_noise(0, 0, 1, 1)));
}

static void compare_region(const World *a, const World *b) {
  for (int x = b->minX; x < b->minX + b->blocksPerSide; x++)
    for (int z = b->minZ; z < b->minZ + b->blocksPerSide; z++)
      for (int y = -32; y < 32; y++)
        assert(world_get_block(a, x, y, z) == world_get_block(b, x, y, z));
}

static void test_terrain_regions(void) {
  World whole = {0}, part = {0}, overlap = {0};
  uint32_t seed = UINT32_C(0x4B1D2A37);
  assert(create_terrain_region(&whole, -16, -16, 32, (Vec3){0}, 1, seed));
  const int orders[2][4] = {{0, 1, 2, 3}, {3, 1, 0, 2}};
  for (int order = 0; order < 2; order++)
    for (int i = 0; i < 4; i++) {
      int tile = orders[order][i];
      assert(create_terrain_region(&part, -16 + (tile / 2) * 16,
                                    -16 + (tile % 2) * 16, 16,
                                    (Vec3){12, -3, 8}, 2, seed));
      compare_region(&whole, &part);
    }
  /* Unequal X/Z section alignment and different world size/transform. */
  assert(create_terrain_region(&overlap, -19, -21, 40, (Vec3){-2, 4, 7}, 0.5f, seed));
  compare_region(&overlap, &whole);
  assert(create_terrain(&part, 17, (Vec3){23, -7, 9}, 0.25f, seed));
  compare_region(&whole, &part);
  size_t changed = 0;
  for (int x = -16; x < 16; x++)
    for (int z = -16; z < 16; z++) {
      TerrainColumn a = terrain_sample_column(seed, x, z);
      TerrainColumn b = terrain_sample_column(seed + 1, x, z);
      changed += a.height != b.height || a.material != b.material;
      for (int y = -32; y < 32; y++)
        assert(world_get_block(&whole, x, y, z) ==
               (y >= -1 && y < a.height ? a.material : BLOCK_TYPE_EMPTY));
    }
  assert(changed > 0);
  Chunk *original = whole.chunks;
  const int invalid[] = {INT_MIN, WORLD_COORD_MIN - 1, WORLD_COORD_MAX, INT_MAX};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
    assert(!create_terrain_region(&whole, invalid[i], 0, 2, (Vec3){0}, 1, seed));
    assert(!create_terrain_region(&whole, 0, invalid[i], 2, (Vec3){0}, 1, seed));
    assert(whole.chunks == original);
  }
  /* Exercise meshing, neighbour reads and float surface queries at both limits. */
  const int edges[] = {WORLD_COORD_MIN, WORLD_COORD_MAX - 16};
  for (int i = 0; i < 2; i++) {
    int edge = edges[i];
    assert(create_terrain_region(&part, edge, edge, 17, (Vec3){0}, 1, seed));
    size_t draws, sections;
    assert(mesh_world(&part, &draws, &sections) > 0);
    for (int x = edge; x < edge + 17; x++) {
      TerrainColumn column = terrain_sample_column(seed, x, edge);
      assert_near(world_surface_height(&part, x + 0.5f, edge + 0.5f), column.height);
    }
  }
  world_destroy(&whole);
  world_destroy(&part);
  world_destroy(&overlap);
}

static void test_generator_behaviour(void) {
  enum { FIXTURE_SIDE = 192 }; /* Explicit scenario, independent of the game default. */
  World world = {0};
  const Vec3 center = {12, -3, 8};
  uint8_t materials[FIXTURE_SIDE * FIXTURE_SIDE], heights[FIXTURE_SIDE * FIXTURE_SIDE];
  for (int terrain = 0; terrain < 2; terrain++) {
    if (terrain)
      assert(create_terrain(&world, FIXTURE_SIDE, center, 2, UINT32_C(0x4B1D2A37)));
    else
      assert(create_platform(&world, FIXTURE_SIDE, center, 2, UINT32_C(0x4B1D2A37)));
    size_t voxels = 0, expectedQuads = 0;
    for (int x = 0; x < FIXTURE_SIDE; x++)
      for (int z = 0; z < FIXTURE_SIDE; z++) {
        int index = x * FIXTURE_SIDE + z;
        float top = world_surface_height(&world, center.x + (x - FIXTURE_SIDE / 2 + 0.5f) * 2,
                                         center.z + (z - FIXTURE_SIDE / 2 + 0.5f) * 2);
        int height = (int)roundf((top - center.y) / 2);
        assert(height >= 0 && height < (WORLD_MIN_SECTION_Y + WORLD_SECTION_COUNT) * SECTION_SIZE);
        heights[index] = (uint8_t)height;
        materials[index] = (uint8_t)world_get_block(&world, x - FIXTURE_SIDE / 2, height - 1, z - FIXTURE_SIDE / 2);
        for (int y = -1; y < height; y++)
          assert(world_get_block(&world, x - FIXTURE_SIDE / 2, y, z - FIXTURE_SIDE / 2) == materials[index]);
        assert(world_get_block(&world, x - FIXTURE_SIDE / 2, height, z - FIXTURE_SIDE / 2) == BLOCK_TYPE_EMPTY);
        voxels += (size_t)height + 1;
      }
    World sameSeed = {0}, otherSeed = {0};
    if (terrain) {
      assert(create_terrain(&sameSeed, FIXTURE_SIDE, center, 2, UINT32_C(0x4B1D2A37)));
      assert(create_terrain(&otherSeed, FIXTURE_SIDE, center, 2, UINT32_C(0x4B1D2A38)));
    } else {
      assert(create_platform(&sameSeed, FIXTURE_SIDE, center, 2, UINT32_C(0x4B1D2A37)));
      assert(create_platform(&otherSeed, FIXTURE_SIDE, center, 2, UINT32_C(0x4B1D2A38)));
    }
    compare_region(&world, &sameSeed);
    size_t changedColumns = 0;
    int minHeight = INT_MAX, maxHeight = INT_MIN;
    for (int x = 0; x < FIXTURE_SIDE; x++)
      for (int z = 0; z < FIXTURE_SIDE; z++) {
        int height = heights[x * FIXTURE_SIDE + z];
        float otherTop = world_surface_height(&otherSeed, center.x + (x - FIXTURE_SIDE / 2 + 0.5f) * 2,
                                               center.z + (z - FIXTURE_SIDE / 2 + 0.5f) * 2);
        BlockType otherType = world_get_block(&otherSeed, x - FIXTURE_SIDE / 2,
                                               (int)roundf((otherTop - center.y) / 2) - 1,
                                               z - FIXTURE_SIDE / 2);
        changedColumns += otherTop != center.y + height * 2 ||
                          otherType != (BlockType)materials[x * FIXTURE_SIDE + z];
        if (height < minHeight) minHeight = height;
        if (height > maxHeight) maxHeight = height;
      }
    assert(changedColumns > 0);
    if (terrain) assert(minHeight < maxHeight);
    else assert(minHeight == 0 && maxHeight == 0);
    world_destroy(&sameSeed);
    world_destroy(&otherSeed);
    assert(world.voxelCount == voxels);
    const int dx[] = {-1, 1, 0, 0}, dz[] = {0, 0, -1, 1};
    for (int x = 0; x < FIXTURE_SIDE; x++)
      for (int z = 0; z < FIXTURE_SIDE; z++) {
        int height = heights[x * FIXTURE_SIDE + z];
        expectedQuads += 2; /* Top and bottom. */
        for (int d = 0; d < 4; d++) {
          int nx = x + dx[d], nz = z + dz[d];
          int neighbor =
              nx < 0 || nx >= FIXTURE_SIDE || nz < 0 || nz >= FIXTURE_SIDE ? -1 : heights[nx * FIXTURE_SIDE + nz];
          if (height > neighbor)
            expectedQuads += (size_t)(height - neighbor);
        }
      }
    size_t draws, sections;
    clock_t start = clock();
    size_t vertices = mesh_world(&world, &draws, &sections);
    double ms = 1000.0 * (clock() - start) / CLOCKS_PER_SEC;
    /* Validate the exposed surface, allowing different face triangulation or merging. */
    assert(fabs(world_mesh_surface_area(&world) - expectedQuads * 4.0) < 0.0001);
    printf("%s: %zu populated meshes, %zu vertices, %zu bytes, %zu draws, CPU %.3f ms\n",
           terrain ? "terrain" : "flat", sections, vertices,
           vertices * sizeof(SurfaceVertex), draws, ms);
  }
  /* Global shuffle balance, zero seed, and odd/small transformed worlds. */
  assert(create_platform(&world, 5, center, 2, 0));
  World repeated = {0};
  assert(create_platform(&repeated, 5, center, 2, 0));
  int counts[BLOCK_TYPE_COUNT] = {0};
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
  for (int type = 0; type < BLOCK_TYPE_COUNT; type++)
    assert(counts[type] >= 25 / BLOCK_TYPE_COUNT &&
           counts[type] <= (25 + BLOCK_TYPE_COUNT - 1) / BLOCK_TYPE_COUNT);
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
    /* A missing section must also replace an existing mesh with no geometry. */
    assert(world_section(&world, 0, 1, 0) == NULL);
    assert(section_mesh_build(&world, 0, 1, 0, &one));
    assert(one.vertices == NULL && one.vertexCount == 0);
    assert(section_mesh_build(&world, voxel_section_coord(a[0]),
                              voxel_section_coord(a[1]), voxel_section_coord(a[2]),
                              &one));
    assert(one.vertexCount == 36);
    assert(world_set_block(&world, b[0], b[1], b[2], BLOCK_TYPE_WATER));
    size_t draws, nonempty;
    assert(mesh_world(&world, &draws, &nonempty) == 60);
    assert(world_set_block(&world, a[0], a[1], a[2], BLOCK_TYPE_EMPTY));
    assert(world_set_block(&world, b[0], b[1], b[2], BLOCK_TYPE_EMPTY));
    /* Retain the empty CPU section so synchronization can retire its GPU mesh. */
    Subchunk *retired = world_section(&world, voxel_section_coord(a[0]),
                                      voxel_section_coord(a[1]),
                                      voxel_section_coord(a[2]));
    assert(retired && retired->voxelCount == 0 && retired->dirty);
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
  const int sections[][4][3] = {
      {{0,0,0}},
      {{0,0,0}, {-1,0,0}},
      {{0,0,0}, {-1,0,0}, {0,-1,0}},
      {{0,0,0}, {-1,0,0}, {0,-1,0}, {0,0,-1}},
      {{0,0,0}, {1,0,0}, {0,1,0}, {0,0,1}},
      {{-1,-1,-1}, {0,-1,-1}, {-1,0,-1}, {-1,-1,0}}};
  for (int i = 0; i < 6; i++) {
    clear_dirty(&world);
    int x = positions[i][0], y = positions[i][1], z = positions[i][2];
    assert(world_set_block(&world, x, y, z, BLOCK_TYPE_GRASS));
    assert_dirty_sections(&world, sections[i], expected[i]);
    clear_dirty(&world);
    assert(world_set_block(&world, x, y, z, BLOCK_TYPE_GRASS));
    assert_dirty_sections(&world, NULL, 0);
    assert(world_set_block(&world, x, y, z, BLOCK_TYPE_EMPTY));
    assert_dirty_sections(&world, sections[i], expected[i]);
  }
  const int faces[][3] = {{0, 2, 2},  {15, 2, 2}, {2, 0, 2},
                          {2, 15, 2}, {2, 2, 0},  {2, 2, 15}};
  const int neighbors[][3] = {{-1,0,0}, {1,0,0}, {0,-1,0}, {0,1,0}, {0,0,-1}, {0,0,1}};
  for (int i = 0; i < 6; i++) {
    clear_dirty(&world);
    assert(
        world_set_block(&world, faces[i][0], faces[i][1], faces[i][2], BLOCK_TYPE_WATER));
    const int affected[][3] = {{0,0,0}, {neighbors[i][0], neighbors[i][1], neighbors[i][2]}};
    assert_dirty_sections(&world, affected, 2);
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

static void test_thin_walls_at_section_boundaries(void) {
  const int boundaries[] = {0, 16, -16};
  const float sizes[] = {0.5f, 1, 2};
  for (int b = 0; b < 3; b++)
    for (int s = 0; s < 3; s++)
      for (int axis = 0; axis < 2; axis++)
        for (int direction = -1; direction <= 1; direction += 2) {
          int boundary = boundaries[b];
          float size = sizes[s];
          World world = {0};
          assert(world_init_region(&world, boundary - 8, boundary - 8, 16,
                                    (Vec3){12, -3, 8}, size));
          int wall = boundary - (direction < 0);
          for (int x = boundary - 8; x < boundary + 8; x++)
            for (int z = boundary - 8; z < boundary + 8; z++) {
              assert(world_set_block(&world, x, -1, z, BLOCK_TYPE_STONE));
              if ((axis == 0 ? x : z) == wall)
                for (int y = 0; y < (int)ceilf(1.5f / size); y++)
                  assert(world_set_block(&world, x, y, z, BLOCK_TYPE_STONE));
            }
          Vec3 start = {12 + (boundary + 0.5f) * size, -3,
                        8 + (boundary + 0.5f) * size};
          float plane = (axis == 0 ? 12 : 8) + boundary * size;
          float approach = plane - direction * (2 * size + 0.25f);
          if (axis == 0)
            start.x = approach;
          else
            start.z = approach;
          Player player = make_player(start, 1);
          player.eyeHeight = 0.1f; /* Camera height cannot shrink the body. */
          PlayerInput input = axis == 0 ? (PlayerInput){.strafe = direction}
                                       : (PlayerInput){.forward = -direction};
          /* Eight world units would clear this one-voxel wall without substeps. */
          update_player(&player, input, 2, &world);
          float contact = axis == 0 ? player.position.x : player.position.z;
          assert_near(contact, plane - direction * 0.25f);
          assert(player.grounded);
          assert_near(player.position.y, start.y);
          /* Sustained contact and tangential movement must remain stable. */
          input = (PlayerInput){.strafe = direction, .forward = -direction};
          for (int frame = 0; frame < 10; frame++)
            update_player(&player, input, 0.01f, &world);
          assert_near(axis == 0 ? player.position.x : player.position.z, contact);
          assert(direction * (axis == 0 ? player.position.z - start.z
                                       : player.position.x - start.x) > 0.25f);
          assert(player.grounded);
          world_destroy(&world);
        }
}

static void test_vertical_contacts_at_section_boundaries(void) {
  const int boundaries[] = {0, 16, -16};
  const float sizes[] = {0.5f, 1, 2};
  for (int b = 0; b < 3; b++)
    for (int s = 0; s < 3; s++) {
      World world = {0};
      float size = sizes[s];
      int floor = boundaries[b] - 1;
      int roof = boundaries[b] + (int)ceilf(2 / size);
      assert(world_init_region(&world, -1, -1, 3, (Vec3){12, -3, 8}, size));
      assert(world_set_block(&world, 0, floor, 0, BLOCK_TYPE_STONE));
      assert(world_set_block(&world, 0, roof, 0, BLOCK_TYPE_STONE));
      float floorY = -3 + boundaries[b] * size;
      float roofY = -3 + roof * size;
      Player player = make_player((Vec3){12 + size / 2, floorY + 0.25f,
                                         8 + size / 2}, 0);
      player.eyeHeight = 10; /* Body dimensions stay in physical world units. */
      player.velocityY = -100;
      update_player(&player, (PlayerInput){0}, 0.25f, &world);
      assert(player.grounded);
      assert_near(player.position.y, floorY);
      assert_near(player.velocityY, 0);
      assert_near(world_surface_height(&world, player.position.x, player.position.z),
                  roofY + size);
      player.grounded = 0;
      player.velocityY = 100;
      update_player(&player, (PlayerInput){0}, 0.02f, &world);
      assert(!player.grounded);
      assert_near(player.position.y, roofY - 1.5f);
      assert_near(player.velocityY, 0);
      for (int frame = 0; frame < 100; frame++)
        update_player(&player, (PlayerInput){0}, 0.01f, &world);
      assert(player.grounded);
      assert_near(player.position.y, floorY);
      assert(world_set_block(&world, 0, floor, 0, BLOCK_TYPE_EMPTY));
      update_player(&player, (PlayerInput){0}, 0.1f, &world);
      assert(!player.grounded && player.position.y < floorY);
      world_destroy(&world);
    }
}

static void test_place_block_on_viewed_face(void) {
  const int directions[6][3] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0},
                                {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
  const int boundaries[] = {-16, 0, 16};
  const float sizes[] = {0.5f, 1, 2};
  for (int b = 0; b < 3; b++)
    for (int s = 0; s < 3; s++)
      for (int d = 0; d < 6; d++) {
        World world = {0};
        int x = boundaries[b], z = boundaries[b];
        float size = sizes[s];
        Vec3 origin = {12, -3, 8};
        assert(world_init_region(&world, x - 8, z - 8, 16, origin, size));
        assert(world_set_block(&world, x, 0, z, BLOCK_TYPE_GRASS));
        Vec3 direction = {directions[d][0], directions[d][1], directions[d][2]};
        Vec3 center = {origin.x + (x + 0.5f) * size, origin.y + 0.5f * size,
                       origin.z + (z + 0.5f) * size};
        Vec3 eye = vec3_sub(center, vec3_scale(direction, 5 * size));
        Player player = make_player(vec3_sub(eye, (Vec3){0, 1, 0}), 0);
        player.eyeHeight = 1;
        player.camera.front = direction;
        assert(player_place_block(&world, &player, BLOCK_TYPE_STONE, 12));
        assert(world_get_block(&world, x - directions[d][0], -directions[d][1],
                               z - directions[d][2]) == BLOCK_TYPE_STONE);
        assert(world_get_block(&world, x, 0, z) == BLOCK_TYPE_GRASS);
        /* The new block participates in meshing and hides the shared face. */
        size_t draws, sections;
        assert(mesh_world(&world, &draws, &sections) == 60);
        world_destroy(&world);
      }
}

static void test_placement_reach_misses_and_player_overlap(void) {
  World world = {0};
  assert(world_init(&world, 32, (Vec3){0}, 1));
  Player player = make_player((Vec3){0.5f, 0, 0.5f}, 0);
  player.eyeHeight = 1;
  player.camera.front = (Vec3){1, 0, 0};
  assert(!player_place_block(&world, &player, BLOCK_TYPE_STONE, 6));
  assert(world_set_block(&world, 7, 1, 0, BLOCK_TYPE_GRASS));
  assert(!player_place_block(&world, &player, BLOCK_TYPE_STONE, 6));
  assert(world_get_block(&world, 6, 1, 0) == BLOCK_TYPE_EMPTY);
  assert(world_set_block(&world, 4, 1, 0, BLOCK_TYPE_GRASS));
  assert(!player_place_block(&world, &player, BLOCK_TYPE_EMPTY, 6));
  assert(!player_place_block(&world, &player, BLOCK_TYPE_STONE, NAN));
  assert(player_place_block(&world, &player, BLOCK_TYPE_STONE, 6));
  assert(world_get_block(&world, 3, 1, 0) == BLOCK_TYPE_STONE);
  assert(world_get_block(&world, 6, 1, 0) == BLOCK_TYPE_EMPTY);
  assert(player_place_block(&world, &player, BLOCK_TYPE_STONE, 6));
  assert(world_get_block(&world, 2, 1, 0) == BLOCK_TYPE_STONE);
  assert(player_place_block(&world, &player, BLOCK_TYPE_STONE, 6));
  assert(world_get_block(&world, 1, 1, 0) == BLOCK_TYPE_STONE);
  assert(!player_place_block(&world, &player, BLOCK_TYPE_STONE, 6));
  assert(world_get_block(&world, 0, 1, 0) == BLOCK_TYPE_EMPTY);
  /* Crouching aims from the lowered eye, rather than the standing eye. */
  update_player(&player, (PlayerInput){.crouch = 1}, 0.001f, &world);
  assert(world_set_block(&world, 4, 0, 0, BLOCK_TYPE_GRASS));
  assert(player_place_block(&world, &player, BLOCK_TYPE_STONE, 6));
  assert(world_get_block(&world, 3, 0, 0) == BLOCK_TYPE_STONE);
  world_destroy(&world);
  /* A hit at the world edge cannot place beyond the supported bounds. */
  assert(world_init_region(&world, 0, 0, 1, (Vec3){0}, 1));
  assert(world_set_block(&world, 0, 0, 0, BLOCK_TYPE_GRASS));
  player = make_player((Vec3){-2, -0.5f, 0.5f}, 0);
  player.eyeHeight = 1;
  player.camera.front = (Vec3){1, 0, 0};
  assert(!player_place_block(&world, &player, BLOCK_TYPE_STONE, 6));
  assert(world_get_block(&world, -1, 0, 0) == BLOCK_TYPE_EMPTY);
  world_destroy(&world);
}

static void test_placement_oblique_view_and_reach_boundary(void) {
  World world = {0};
  assert(world_init(&world, 16, (Vec3){0}, 1));
  assert(world_set_block(&world, 3, 1, 1, BLOCK_TYPE_GRASS));
  Player player = make_player((Vec3){0.5f, 0.5f, 0.5f}, 0);
  player.eyeHeight = 1;
  player.camera.front = (Vec3){3, 0, 1};
  assert(player_place_block(&world, &player, BLOCK_TYPE_STONE, 6));
  assert(world_get_block(&world, 2, 1, 1) == BLOCK_TYPE_STONE);
  world_destroy(&world);
  assert(world_init(&world, 16, (Vec3){0}, 1));
  assert(world_set_block(&world, 3, 1, 0, BLOCK_TYPE_GRASS));
  player.camera.front = (Vec3){1, 0, 0};
  assert(!player_place_block(&world, &player, BLOCK_TYPE_STONE, 2.49f));
  assert(player_place_block(&world, &player, BLOCK_TYPE_STONE, 2.5f));
  assert(world_get_block(&world, 2, 1, 0) == BLOCK_TYPE_STONE);
  /* A view starting inside a block cannot build through it. */
  assert(world_set_block(&world, 0, 1, 0, BLOCK_TYPE_GRASS));
  assert(!player_place_block(&world, &player, BLOCK_TYPE_STONE, 6));
  assert(world_get_block(&world, 1, 1, 0) == BLOCK_TYPE_EMPTY);
  world_destroy(&world);
}

static void test_remove_nearest_block_and_update_mesh(void) {
  const float sizes[] = {0.5f, 1, 2};
  const int directions[6][3] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0},
                                {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
  for (int s = 0; s < 3; s++)
    for (int d = 0; d < 6; d++) {
      World world = {0};
      float size = sizes[s];
      Vec3 origin = {12, -3, 8};
      int x = -16, y = 0, z = 0;
      Vec3 direction = {directions[d][0], directions[d][1], directions[d][2]};
      assert(world_init_region(&world, -24, -8, 16, origin, size));
      assert(world_set_block(&world, x, y, z, BLOCK_TYPE_STONE));
      assert(world_set_block(&world, x + directions[d][0], y + directions[d][1],
                             z + directions[d][2], BLOCK_TYPE_GRASS));
      Vec3 center = {origin.x + (x + 0.5f) * size, origin.y + 0.5f * size,
                     origin.z + 0.5f * size};
      Vec3 eye = vec3_sub(center, vec3_scale(direction, 2 * size));
      Player player = make_player(vec3_sub(eye, (Vec3){0, 1, 0}), 0);
      player.eyeHeight = 1;
      player.camera.front = direction;
      size_t draws, sections;
      assert(mesh_world(&world, &draws, &sections) == 60);
      assert(player_remove_block(&world, &player, 6));
      assert(world_get_block(&world, x, y, z) == BLOCK_TYPE_EMPTY);
      assert(world_get_block(&world, x + directions[d][0], y + directions[d][1],
                             z + directions[d][2]) == BLOCK_TYPE_GRASS);
      assert(mesh_world(&world, &draws, &sections) == 36);
      assert(player_remove_block(&world, &player, 6));
      assert(mesh_world(&world, &draws, &sections) == 0);
      assert(!player_remove_block(&world, &player, 6));
      world_destroy(&world);
    }
}

static void test_removal_reach_crouched_aim_and_collision(void) {
  World world = {0};
  assert(world_init(&world, 16, (Vec3){0}, 1));
  Player player = make_player((Vec3){0.5f, 0, 0.5f}, 0);
  player.eyeHeight = 1;
  player.camera.front = (Vec3){1, 0, 0};
  assert(world_set_block(&world, 7, 1, 0, BLOCK_TYPE_STONE));
  assert(!player_remove_block(&world, &player, 6));
  assert(world_get_block(&world, 7, 1, 0) == BLOCK_TYPE_STONE);
  assert(world_set_block(&world, 3, 1, 0, BLOCK_TYPE_GRASS));
  assert(!player_remove_block(&world, &player, 2.49f));
  assert(!player_remove_block(&world, &player, NAN));
  assert(player_remove_block(&world, &player, 2.5f));
  assert(world_get_block(&world, 3, 1, 0) == BLOCK_TYPE_EMPTY);
  assert(world_set_block(&world, 3, 0, 0, BLOCK_TYPE_SAND));
  update_player(&player, (PlayerInput){.crouch = 1}, 0.001f, &world);
  assert(player_remove_block(&world, &player, 6));
  assert(world_get_block(&world, 3, 0, 0) == BLOCK_TYPE_EMPTY);
  assert(world_get_block(&world, 7, 1, 0) == BLOCK_TYPE_STONE);
  world_destroy(&world);
  world = single_block_world();
  player = make_player((Vec3){0}, 1);
  player.eyeHeight = 1;
  player.camera.front = (Vec3){0, -1, 0};
  assert(player_remove_block(&world, &player, 6));
  update_player(&player, (PlayerInput){0}, 0.1f, &world);
  assert(!player.grounded && player.position.y < 0);
  world_destroy(&world);
}

static void test_generated_terrain_spawn(void) {
  const float sizes[] = {0.5f, 1, 2};
  for (int s = 0; s < 3; s++) {
    World world = {0};
    Vec3 center = {12, -3, 8};
    assert(create_terrain(&world, 32, center, sizes[s], UINT32_C(0x4B1D2A37)));
    float spawnY = world_surface_height(&world, center.x, center.z);
    assert(isfinite(spawnY));
    Player player = make_player((Vec3){center.x, spawnY, center.z}, 0);
    for (int frame = 0; frame < 60; frame++)
      update_player(&player, (PlayerInput){0}, 1.0f / 60, &world);
    assert(player.grounded);
    assert_near(player.position.y, spawnY);
    update_player(&player, (PlayerInput){.jump = 1}, 1.0f / 60, &world);
    assert(!player.grounded && player.position.y > spawnY);
    for (int frame = 0; frame < 60; frame++)
      update_player(&player, (PlayerInput){0}, 1.0f / 60, &world);
    assert(player.grounded);
    assert_near(player.position.y, spawnY);
    world_destroy(&world);
  }
}

static void test_default_world_terrain_and_boundaries(void) {
  World world = {0};
  uint32_t seed = UINT32_C(0x4B1D2A37);
  int side = PLATFORM_BLOCKS_PER_SIDE;
  clock_t start = clock();
  assert(create_terrain(&world, side, (Vec3){0}, 1, seed));
  int first = -(side / 2), last = first + side - 1;
  const int samples[] = {first, first + side / 4, first + side / 2,
                          first + 3 * side / 4, last};
  for (size_t x = 0; x < sizeof(samples) / sizeof(*samples); x++)
    for (size_t z = 0; z < sizeof(samples) / sizeof(*samples); z++) {
      int vx = samples[x], vz = samples[z];
      TerrainColumn column = terrain_sample_column(seed, vx, vz);
      assert(world_get_block(&world, vx, column.height - 1, vz) == column.material);
      assert_near(world_surface_height(&world, vx + 0.5f, vz + 0.5f), column.height);
      assert(world_get_block(&world, vx, column.height, vz) == BLOCK_TYPE_EMPTY);
      /* Edits work throughout the expanded area, including the furthest corners. */
      assert(world_set_block(&world, vx, column.height, vz, BLOCK_TYPE_STONE));
      assert_near(world_surface_height(&world, vx + 0.5f, vz + 0.5f), column.height + 1);
      assert(world_set_block(&world, vx, column.height, vz, BLOCK_TYPE_EMPTY));
    }
  assert(!world_set_block(&world, -side / 2 - 1, 0, 0, BLOCK_TYPE_STONE));
  assert(!world_set_block(&world, last + 1, 0, 0, BLOCK_TYPE_STONE));
  assert(!world_set_block(&world, 0, 0, -side / 2 - 1, BLOCK_TYPE_STONE));
  assert(!world_set_block(&world, 0, 0, last + 1, BLOCK_TYPE_STONE));
  float spawnY = world_surface_height(&world, 0.5f, 0.5f);
  Player player = make_player((Vec3){0.5f, spawnY, 0.5f}, 0);
  for (int frame = 0; frame < 60; frame++)
    update_player(&player, (PlayerInput){0}, 1.0f / 60, &world);
  assert(player.grounded);
  assert_near(player.position.y, spawnY);
  size_t draws, sections;
  size_t vertices = mesh_world(&world, &draws, &sections);
  assert(vertices > 0);
  printf("Expanded %dx%d world: %zu vertices, CPU generation + meshing %.3f ms\n",
         side, side, vertices, 1000.0 * (clock() - start) / CLOCKS_PER_SEC);
  world_destroy(&world);

}

int main(void) {
  test_vector_normalization();
  test_matrix_multiply_identity();
  test_block_type_palette();
  test_diagonal_movement_is_normalized();
  test_running_and_crouching_speed();
  test_crouching_lowers_view_until_released();
  test_landing_on_block();
  test_falling_off_platform_edge();
  test_jump_requires_grounding();
  test_jump_height_and_timing();
  test_random_bounded_output_range();
  test_terrain_collision();
  test_camera_stays_clear_of_walls();
  test_player_footprint_at_corners_and_ledges();
  test_coordinates_and_storage();
  test_world_size_contract();
  test_signed_terrain_sampling();
  test_terrain_regions();
  test_generator_behaviour();
  test_section_faces_and_replacement();
  test_dirty_neighbors();
  test_cave_and_ceiling_collision();
  test_thin_walls_at_section_boundaries();
  test_vertical_contacts_at_section_boundaries();
  test_generated_terrain_spawn();
  test_default_world_terrain_and_boundaries();
  test_place_block_on_viewed_face();
  test_placement_reach_misses_and_player_overlap();
  test_placement_oblique_view_and_reach_boundary();
  test_remove_nearest_block_and_update_mesh();
  test_removal_reach_crouched_aim_and_collision();
  puts("All game logic tests passed.");
  return 0;
}
