#include "../game_logic.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/* Linker wrappers allow deterministic failure at every allocation without
   shipping allocator hooks in the world or renderer. */
void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
static int fail_after = -1;
static size_t allocation_calls;
static int reject_allocation(void) {
  if (fail_after < 0)
    return 0;
  if (fail_after == 0)
    return 1;
  fail_after--;
  return 0;
}
void *__wrap_malloc(size_t size) {
  allocation_calls++;
  return reject_allocation() ? NULL : __real_malloc(size);
}
void *__wrap_calloc(size_t count, size_t size) {
  allocation_calls++;
  return reject_allocation() ? NULL : __real_calloc(count, size);
}

static int generate_world(int generator, World *world) {
  if (generator == 0)
    return create_platform(world, 17, (Vec3){12, -3, 8}, 2, 1);
  if (generator == 1)
    return create_terrain(world, 17, (Vec3){12, -3, 8}, 2, 1);
  return create_terrain_region(world, -17, 15, 17, (Vec3){12, -3, 8}, 2, 1);
}

static void assert_world_equal(const World *actual, const World *expected) {
  assert(actual->minX == expected->minX && actual->minZ == expected->minZ);
  assert(actual->blocksPerSide == expected->blocksPerSide);
  assert(actual->blockSize == expected->blockSize);
  assert(actual->origin.x == expected->origin.x && actual->origin.y == expected->origin.y &&
         actual->origin.z == expected->origin.z);
  for (int x = expected->minX; x < expected->minX + expected->blocksPerSide; x++)
    for (int z = expected->minZ; z < expected->minZ + expected->blocksPerSide; z++)
      for (int y = WORLD_MIN_SECTION_Y * SECTION_SIZE;
           y < (WORLD_MIN_SECTION_Y + WORLD_SECTION_COUNT) * SECTION_SIZE; y++)
        assert(world_get_block(actual, x, y, z) == world_get_block(expected, x, y, z));
}

int main(void) {
  World world = {0};
  assert(world_init(&world, 32, (Vec3){0}, 1));
  fail_after = 0;
  assert(!world_set_block(&world, -1, 0, 0, BLOCK_TYPE_GRASS));
  assert(world.voxelCount == 0 && world.sectionCount == 0);
  assert(world_get_block(&world, -1, 0, 0) == BLOCK_TYPE_EMPTY);
  fail_after = -1;
  assert(world_set_block(&world, -1, 0, 0, BLOCK_TYPE_GRASS));
  SectionMesh mesh = {0};
  assert(section_mesh_build(&world, -1, 0, 0, &mesh));
  SurfaceVertex *original = mesh.vertices;
  SectionMesh originalMesh = mesh;
  size_t originalCount = mesh.vertexCount;
  SurfaceVertex *saved = malloc(originalCount * sizeof(*saved));
  assert(saved);
  for (size_t i = 0; i < originalCount; i++) saved[i] = mesh.vertices[i];
  Subchunk *section = world_section(&world, -1, 0, 0);
  fail_after = 0;
  assert(!section_mesh_build(&world, -1, 0, 0, &mesh));
  assert(mesh.vertices == original && mesh.vertexCount == originalCount && section->dirty);
  for (int type = 0; type < BLOCK_TYPE_COUNT; type++) {
    assert(mesh.firstVertex[type] == originalMesh.firstVertex[type]);
    assert(mesh.typeVertexCount[type] == originalMesh.typeVertexCount[type]);
  }
  for (size_t i = 0; i < originalCount; i++) {
    assert(mesh.vertices[i].x == saved[i].x);
    assert(mesh.vertices[i].y == saved[i].y);
    assert(mesh.vertices[i].z == saved[i].z);
  }
  fail_after = -1;
  free(saved);
  section_mesh_destroy(&mesh);
  /* Reject every allocation in each generator; ownership and content survive. */
  for (int generator = 0; generator < 3; generator++) {
    World reference = {0}, sentinel = {0};
    size_t before = allocation_calls;
    assert(generate_world(generator, &reference));
    size_t allocations = allocation_calls - before;
    assert(world_init(&sentinel, 32, (Vec3){0}, 1));
    assert(world_set_block(&sentinel, -1, 0, 0, BLOCK_TYPE_GRASS));
    /* Discover allocation sites from a successful run, rather than guessing a
       fixed count. A fallback may succeed; it must still produce the full result. */
    for (size_t i = 0; i < allocations; i++) {
      world_destroy(&world);
      assert(world_init(&world, 32, (Vec3){0}, 1));
      assert(world_set_block(&world, -1, 0, 0, BLOCK_TYPE_GRASS));
      Chunk *chunks = world.chunks;
      Subchunk *originalSection = world_section(&world, -1, 0, 0);
      fail_after = (int)i;
      int success = generate_world(generator, &world);
      fail_after = -1;
      if (success) {
        assert_world_equal(&world, &reference);
      } else {
        /* Ownership is explicitly transactional; check data as well as identity. */
        assert(world.chunks == chunks);
        assert(world_section(&world, -1, 0, 0) == originalSection);
        assert_world_equal(&world, &sentinel);
        assert(world_set_block(&world, 0, 0, 0, BLOCK_TYPE_STONE));
        assert(world_get_block(&world, 0, 0, 0) == BLOCK_TYPE_STONE);
      }
    }
    assert(generate_world(generator, &world));
    assert_world_equal(&world, &reference);
    world_destroy(&reference);
    world_destroy(&sentinel);
  }
  world_destroy(&world);
  /* Chunk generation commits all its sections together, including under OOM. */
  World streamed = {0};
  assert(create_terrain_streamed(&streamed, 33, (Vec3){0}, 1, 1));
  size_t before = allocation_calls;
  assert(world_materialize_chunk(&streamed, 0, 0));
  size_t allocations = allocation_calls - before;
  for (size_t i = 0; i < allocations; i++) {
    world_destroy(&streamed);
    assert(create_terrain_streamed(&streamed, 33, (Vec3){0}, 1, 1));
    TerrainColumn column = terrain_sample_column(1, 0, 0);
    fail_after = (int)i;
    assert(!world_materialize_chunk(&streamed, 0, 0));
    fail_after = -1;
    assert(streamed.sectionCount == 0 && streamed.voxelCount == 0);
    for (int sy = WORLD_MIN_SECTION_Y; sy < WORLD_MIN_SECTION_Y + WORLD_SECTION_COUNT; sy++)
      assert(world_section(&streamed, 0, sy, 0) == NULL);
    assert(world_get_block(&streamed, 0, column.height - 1, 0) == column.material);
    assert(world_materialize_chunk(&streamed, 0, 0));
    assert(world_get_block(&streamed, 0, column.height - 1, 0) == column.material);
  }
  world_destroy(&streamed);
  puts("All allocation failure tests passed.");
  return 0;
}
