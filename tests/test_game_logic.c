#include "../game_logic.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static void assert_near(float actual, float expected) {
  assert(fabsf(actual - expected) < 0.0001f);
}

static World single_block_world(void) {
  World world = {0};
  assert(create_platform(&world, 1, (Vec3){0.0f, 0.0f, 0.0f}, 1.0f, 1));
  return world;
}

static Player make_player(Vec3 position, int grounded) {
  return (Player){
      .position = position,
      .grounded = grounded,
      .camera = {
          .front = {0.0f, 0.0f, -1.0f},
          .up = {0.0f, 1.0f, 0.0f},
      },
  };
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
  mat4 original = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f,
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
      [BLOCK_TYPE_GRASS] = {0.38f, 0.62f, 0.42f},
      [BLOCK_TYPE_STONE] = {0.55f, 0.58f, 0.60f},
      [BLOCK_TYPE_WATER] = {0.20f, 0.48f, 0.78f},
      [BLOCK_TYPE_SAND] = {0.86f, 0.72f, 0.42f},
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
      assert(colors[first].x != colors[second].x ||
             colors[first].y != colors[second].y ||
             colors[first].z != colors[second].z);
    }
  }

  Vec3 color = {9.0f, 9.0f, 9.0f};
  assert(!block_type_color((BlockType)-1, &color));
  assert(!block_type_color(BLOCK_TYPE_COUNT, &color));
  assert(!block_type_color(BLOCK_TYPE_GRASS, NULL));
  assert_near(color.x, 9.0f);
}

static void test_platform_generates_seeded_block_types(void) {
  World first = {0};
  World sameSeed = {0};
  World differentSeed = {0};
  World zeroSeed = {0};
  World sameZeroSeed = {0};
  Vec3 center = {0.0f, 0.0f, 0.0f};
  assert(create_platform(&first, 8, center, 1.0f, 42));
  assert(create_platform(&sameSeed, 8, center, 1.0f, 42));
  assert(create_platform(&differentSeed, 8, center, 1.0f, 43));
  assert(create_platform(&zeroSeed, 8, center, 1.0f, 0));
  assert(create_platform(&sameZeroSeed, 8, center, 1.0f, 0));
  assert(first.blockCount == 64);
  assert_near(first.blocks[0].position.x, -3.5f);
  assert_near(first.blocks[0].position.y, -0.5f);
  assert_near(first.blocks[0].position.z, -3.5f);

  int typeCounts[BLOCK_TYPE_COUNT] = {0};
  int matchingNeighbors = 0;
  int differsFromOtherSeed = 0;
  for (int i = 0; i < first.blockCount; i++) {
    assert(first.blocks[i].type == sameSeed.blocks[i].type);
    assert(zeroSeed.blocks[i].type == sameZeroSeed.blocks[i].type);
    assert(first.blocks[i].position.x == sameSeed.blocks[i].position.x);
    assert(first.blocks[i].position.y == sameSeed.blocks[i].position.y);
    assert(first.blocks[i].position.z == sameSeed.blocks[i].position.z);
    if (first.blocks[i].type != differentSeed.blocks[i].type)
      differsFromOtherSeed = 1;
    assert(first.blocks[i].type >= BLOCK_TYPE_GRASS);
    assert(first.blocks[i].type < BLOCK_TYPE_COUNT);
    typeCounts[first.blocks[i].type]++;

    int x = i / 8;
    int z = i % 8;
    if (x + 1 < 8 &&
        first.blocks[i].type == first.blocks[i + 8].type)
      matchingNeighbors++;
    if (z + 1 < 8 &&
        first.blocks[i].type == first.blocks[i + 1].type)
      matchingNeighbors++;
  }
  assert(differsFromOtherSeed);
  assert(matchingNeighbors < 56);
  for (int type = 0; type < BLOCK_TYPE_COUNT; type++)
    assert(typeCounts[type] == first.blockCount / BLOCK_TYPE_COUNT);
}

static void test_platform_balances_non_divisible_type_counts(void) {
  World world = {0};
  assert(create_platform(&world, 5, (Vec3){0.0f, 0.0f, 0.0f}, 1.0f, 99));
  assert(world.blockCount == 25);

  int typeCounts[BLOCK_TYPE_COUNT] = {0};
  for (int i = 0; i < world.blockCount; i++)
    typeCounts[world.blocks[i].type]++;

  int minimum = typeCounts[0];
  int maximum = typeCounts[0];
  for (int type = 1; type < BLOCK_TYPE_COUNT; type++) {
    if (typeCounts[type] < minimum)
      minimum = typeCounts[type];
    if (typeCounts[type] > maximum)
      maximum = typeCounts[type];
  }
  assert(minimum == 6);
  assert(maximum == 7);
}

static void test_configured_platform_geometry_and_seed(void) {
  World world = {0};
  World repeated = {0};
  Vec3 center = {0.0f, 0.0f, 0.0f};
  const float blockSize = 1.0f;
  const int side = PLATFORM_BLOCKS_PER_SIDE;
  const int totalBlocks = side * side;

  assert(side == 192);
  assert(create_platform(&world, side, center, blockSize, UINT32_C(0x4B1D2A37)));
  assert(create_platform(&repeated, side, center, blockSize,
                         UINT32_C(0x4B1D2A37)));
  assert(world.blockCount == totalBlocks);

  int typeCounts[BLOCK_TYPE_COUNT] = {0};
  for (int x = 0; x < side; x++) {
    for (int z = 0; z < side; z++) {
      int index = x * side + z;
      const Block *block = &world.blocks[index];
      const Block *sameSeedBlock = &repeated.blocks[index];
      float expectedX = center.x + ((float)x - side / 2.0f + 0.5f) * blockSize;
      float expectedZ = center.z + ((float)z - side / 2.0f + 0.5f) * blockSize;

      assert_near(block->position.x, expectedX);
      assert_near(block->position.y, center.y - blockSize / 2.0f);
      assert_near(block->position.z, expectedZ);
      assert_near(block->size.x, blockSize);
      assert_near(block->size.y, blockSize);
      assert_near(block->size.z, blockSize);
      assert(block->type >= BLOCK_TYPE_GRASS);
      assert(block->type < BLOCK_TYPE_COUNT);
      assert(block->type == sameSeedBlock->type);
      typeCounts[block->type]++;
    }
  }

  for (int type = 0; type < BLOCK_TYPE_COUNT; type++)
    assert(typeCounts[type] == totalBlocks / BLOCK_TYPE_COUNT);
}

static void test_platform_surface_mesh(void) {
  World single = {0};
  PlatformSurfaceMesh singleMesh = {0};
  assert(create_platform(&single, 1, (Vec3){0.0f, 0.0f, 0.0f}, 1.0f, 1));
  assert(platform_surface_mesh_build(&single, 1, &singleMesh));
  assert(singleMesh.vertexCount == 30);
  for (int type = 0; type < BLOCK_TYPE_COUNT; type++) {
    if ((BlockType)type == single.blocks[0].type)
      assert(singleMesh.typeVertexCount[type] == 30);
    else
      assert(singleMesh.typeVertexCount[type] == 0);
  }
  platform_surface_mesh_destroy(&singleMesh);
  assert(singleMesh.vertices == NULL);
  assert(singleMesh.vertexCount == 0);

  World world = {0};
  PlatformSurfaceMesh mesh = {0};
  assert(create_platform(&world, 2, (Vec3){0.0f, 0.0f, 0.0f}, 1.0f, 2));
  assert(platform_surface_mesh_build(&world, 2, &mesh));
  assert(mesh.vertexCount == (2 * 2 + 4 * 2) * 6);

  size_t topTriangles = 0;
  size_t negativeXTriangles = 0;
  size_t positiveXTriangles = 0;
  size_t negativeZTriangles = 0;
  size_t positiveZTriangles = 0;
  for (size_t index = 0; index < mesh.vertexCount; index += 3) {
    const SurfaceVertex *first = &mesh.vertices[index];
    const SurfaceVertex *second = &mesh.vertices[index + 1];
    const SurfaceVertex *third = &mesh.vertices[index + 2];
    float edge1x = second->x - first->x;
    float edge1y = second->y - first->y;
    float edge1z = second->z - first->z;
    float edge2x = third->x - first->x;
    float edge2y = third->y - first->y;
    float edge2z = third->z - first->z;
    float normalX = edge1y * edge2z - edge1z * edge2y;
    float normalY = edge1z * edge2x - edge1x * edge2z;
    float normalZ = edge1x * edge2y - edge1y * edge2x;

    if (normalY > 0.0f)
      topTriangles++;
    else if (normalX < 0.0f)
      negativeXTriangles++;
    else if (normalX > 0.0f)
      positiveXTriangles++;
    else if (normalZ < 0.0f)
      negativeZTriangles++;
    else if (normalZ > 0.0f)
      positiveZTriangles++;
    assert(first->x >= -1.0f && first->x <= 1.0f);
    assert(first->y >= -1.0f && first->y <= 0.0f);
    assert(first->z >= -1.0f && first->z <= 1.0f);
    assert(second->x >= -1.0f && second->x <= 1.0f);
    assert(second->y >= -1.0f && second->y <= 0.0f);
    assert(second->z >= -1.0f && second->z <= 1.0f);
    assert(third->x >= -1.0f && third->x <= 1.0f);
    assert(third->y >= -1.0f && third->y <= 0.0f);
    assert(third->z >= -1.0f && third->z <= 1.0f);
  }
  assert(topTriangles == 2 * 2 * 2);
  assert(negativeXTriangles == 4);
  assert(positiveXTriangles == 4);
  assert(negativeZTriangles == 4);
  assert(positiveZTriangles == 4);

  size_t previousEnd = 0;
  size_t expectedTypeVertices[BLOCK_TYPE_COUNT] = {0};
  for (int x = 0; x < 2; x++) {
    for (int z = 0; z < 2; z++) {
      const Block *block = &world.blocks[x * 2 + z];
      size_t faces = 1 + (x == 0) + (x == 1) + (z == 0) + (z == 1);
      expectedTypeVertices[block->type] += faces * 6;
    }
  }
  for (int type = 0; type < BLOCK_TYPE_COUNT; type++) {
    assert(mesh.firstVertex[type] == previousEnd);
    assert(mesh.typeVertexCount[type] % 6 == 0);
    assert(mesh.typeVertexCount[type] == expectedTypeVertices[type]);
    previousEnd += mesh.typeVertexCount[type];
  }
  assert(previousEnd == mesh.vertexCount);

  PlatformSurfaceMesh invalidMesh = {0};
  assert(!platform_surface_mesh_build(&world, 1, &invalidMesh));
  assert(invalidMesh.vertices == NULL);
  platform_surface_mesh_destroy(&mesh);
}

static void test_platform_rejects_invalid_requests_without_mutation(void) {
  World world = {0};
  Block original = {
      .position = {1.0f, 2.0f, 3.0f},
      .size = {4.0f, 5.0f, 6.0f},
      .type = BLOCK_TYPE_STONE,
  };
  assert(world_add_block(&world, original));

  assert(!create_platform(&world, 0, (Vec3){0.0f, 0.0f, 0.0f}, 1.0f, 1));
  assert(!create_platform(&world, -1, (Vec3){0.0f, 0.0f, 0.0f}, 1.0f, 1));
  assert(!create_platform(&world, PLATFORM_BLOCKS_PER_SIDE + 1,
                          (Vec3){0.0f, 0.0f, 0.0f}, 1.0f, 1));
  assert(!create_platform(NULL, 1, (Vec3){0.0f, 0.0f, 0.0f}, 1.0f, 1));
  assert(world.blockCount == 1);
  assert(world.blocks[0].position.x == original.position.x);
  assert(world.blocks[0].position.y == original.position.y);
  assert(world.blocks[0].position.z == original.position.z);
  assert(world.blocks[0].type == original.type);
}

static void test_world_rejects_invalid_block_type(void) {
  World world = {0};
  Block block = {.type = (BlockType)-1};
  assert(!world_add_block(&world, block));
  assert(world.blockCount == 0);
}

static void test_diagonal_movement_is_normalized(void) {
  World world = {0};
  Player forward = make_player((Vec3){0.0f, 10.0f, 0.0f}, 0);
  Player diagonal = forward;
  update_player(&forward, (PlayerInput){.forward = 1.0f}, 0.25f, &world);
  update_player(&diagonal,
                (PlayerInput){.forward = 1.0f, .strafe = 1.0f}, 0.25f,
                &world);

  float forwardDistance = fabsf(forward.position.z);
  float diagonalDistance = hypotf(diagonal.position.x,
                                  diagonal.position.z);
  assert_near(forwardDistance, 1.0f);
  assert_near(diagonalDistance, forwardDistance);
}

static void test_landing_on_block(void) {
  World world = single_block_world();
  Player player = make_player((Vec3){0.0f, 0.1f, 0.0f}, 0);
  player.velocityY = -1.0f;

  update_player(&player, (PlayerInput){0}, 0.1f, &world);

  assert(player.grounded);
  assert_near(player.position.y, 0.0f);
  assert_near(player.velocityY, 0.0f);
}

static void test_falling_off_platform_edge(void) {
  World world = single_block_world();
  Player player = make_player((Vec3){0.0f, 0.0f, 0.0f}, 1);

  update_player(&player, (PlayerInput){.forward = 1.0f}, 0.2f, &world);

  assert(!player.grounded);
  assert(player.position.z < -0.5f);
  assert(player.position.y < 0.0f);
  assert(player.velocityY < 0.0f);
}

static void test_jump_requires_grounding(void) {
  World world = single_block_world();
  Player grounded = make_player((Vec3){0.0f, 0.0f, 0.0f}, 1);
  Player airborne = make_player((Vec3){0.0f, 2.0f, 0.0f}, 0);

  update_player(&grounded, (PlayerInput){.jump = 1}, 0.1f, &world);
  update_player(&airborne, (PlayerInput){.jump = 1}, 0.1f, &world);

  assert(!grounded.grounded);
  assert(grounded.velocityY > 0.0f);
  assert(grounded.position.y > 0.0f);
  assert(!airborne.grounded);
  assert_near(airborne.velocityY, -9.81f * 0.1f);
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

int main(void) {
  test_vector_normalization();
  test_matrix_multiply_identity();
  test_block_type_palette();
  test_platform_generates_seeded_block_types();
  test_platform_balances_non_divisible_type_counts();
  test_configured_platform_geometry_and_seed();
  test_platform_surface_mesh();
  test_platform_rejects_invalid_requests_without_mutation();
  test_world_rejects_invalid_block_type();
  test_diagonal_movement_is_normalized();
  test_landing_on_block();
  test_falling_off_platform_edge();
  test_jump_requires_grounding();
  test_random_bounded_output_range();
  puts("All game logic tests passed.");
  return 0;
}