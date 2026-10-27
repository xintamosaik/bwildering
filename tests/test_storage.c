#include "../world_generation.h"
#include "../game_logic.h"
#include "../storage.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static SurfaceVertex scratch[SECTION_MESH_MAX_VERTICES];
static void exhausted(const size_t nodes, const size_t payloads) {
  World world = {0};
  assert(world_init(&world, 32, (Vec3){0}, 1.0f));
  assert(world_storage_limits(&world, nodes, payloads));
  assert(!world_set_block(&world, 0, 0, 0, BLOCK_TYPE_SNOW));
  assert(!world.sectionCount && !world.voxelCount);
  assert(world_get_block(&world, 0, 0, 0) == BLOCK_TYPE_EMPTY);
  world_destroy(&world);
}
static void test_pool_reuse(void) {
  World a = {0}, b = {0};
  assert(create_terrain_streamed(&a, 32, (Vec3){0}, 1.0f, 1));
  assert(create_terrain_streamed(&b, 32, (Vec3){0}, 1.0f, 1));
  assert(world_storage_limits(&a, 1, 1));
  assert(world_storage_limits(&b, 1, 1));
  assert(world_set_block(&a, 0, 500, 0, BLOCK_TYPE_SNOW));
  assert(world_set_block(&b, 0, 500, 0, BLOCK_TYPE_STONE));
  const Subchunk *const old = world_section(&a, 0, 31, 0);
  const uint8_t *const payload = old->blocks;
  assert(old != world_section(&b, 0, 31, 0));
  assert(!world_set_block(&a, -16, 500, 0, BLOCK_TYPE_GRASS));
  assert(world_get_block(&a, 0, 500, 0) == BLOCK_TYPE_SNOW);
  const size_t nodes = storage_stats(STORAGE_NODE)->systemAllocations;
  const size_t payloads = storage_stats(STORAGE_PAYLOAD)->systemAllocations;
  world_release_chunk(&a, 0, 0);
  assert(world_set_block(&a, -16, 500, 0, BLOCK_TYPE_GRASS));
  assert(world_section(&a, -1, 31, 0) == old);
  assert(world_section(&a, -1, 31, 0)->blocks == payload);
  assert(world_get_block(&a, -15, 500, 0) == BLOCK_TYPE_EMPTY);
  assert(world_get_block(&b, 0, 500, 0) == BLOCK_TYPE_STONE);
  assert(storage_stats(STORAGE_NODE)->systemAllocations == nodes);
  assert(storage_stats(STORAGE_PAYLOAD)->systemAllocations == payloads);
  world_destroy(&a);
  world_destroy(&b);
}
static void test_payload_exhaustion(void) {
  World world = {0};
  assert(world_init(&world, 32, (Vec3){0}, 1.0f));
  assert(world_storage_limits(&world, 2, 1));
  assert(world_set_block(&world, 0, 0, 0, BLOCK_TYPE_GRASS));
  assert(!world_set_block(&world, -16, 0, 0, BLOCK_TYPE_STONE));
  assert(world.sectionCount == 1 && world.voxelCount == 1);
  assert(world_set_block(&world, 0, 0, 0, BLOCK_TYPE_EMPTY));
  assert(world_set_block(&world, -16, 0, 0, BLOCK_TYPE_STONE));
  world_destroy(&world);
}
static void test_staging_exhaustion(void) {
  World world = {0};
  assert(create_terrain_streamed(&world, 32, (Vec3){0}, 1.0f, 1));
  assert(world_storage_limits(&world, 2, 1));
  const BlockType before = world_get_block(&world, 0, 0, 0);
  for (int attempt = 0; attempt < 2; attempt++) {
    assert(!world_materialize_chunk(&world, 0, 0));
    assert(!world.sectionCount && !world.voxelCount && !world.hasMaterializedBounds);
    assert(world_get_block(&world, 0, 0, 0) == before);
    assert(!storage_stats(STORAGE_NODE)->live && !storage_stats(STORAGE_PAYLOAD)->live);
  }
  world_destroy(&world);
}
static void test_scratch(void) {
  World world = {0};
  assert(world_init_region(&world, 0, 0, 16, (Vec3){-7.0f, 4.0f, 11.0f}, 0.5f));
  /* Checkerboard forces all six faces of every solid cell to be emitted. */
  for (int x = 0; x < SECTION_SIZE; x++)
    for (int y = 0; y < SECTION_SIZE; y++)
      for (int z = 0; z < SECTION_SIZE; z++)
        if ((x + y + z) % 2 == 0)
          assert(world_set_block(&world, x, y, z, (BlockType)((x + z) % BLOCK_TYPE_COUNT)));
  SectionMesh owning = {0}, borrowed = {0};
  assert(section_mesh_build(&world, 0, 0, 0, &owning));
  assert(owning.vertexCount == SECTION_VOLUME / 2 * 36);
  borrowed.vertexCount = 123;
  assert(!section_mesh_build_scratch(&world, 0, 0, 0, scratch, owning.vertexCount - 1, &borrowed));
  assert(borrowed.vertexCount == 123 && !borrowed.vertices);
  assert(!section_mesh_build_scratch(&world, 0, 0, 0, NULL, SECTION_MESH_MAX_VERTICES, &borrowed));
  assert(section_mesh_build_scratch(&world, 0, 0, 0, scratch, SECTION_MESH_MAX_VERTICES, &borrowed));
  assert(borrowed.vertices == scratch && borrowed.vertexCount == owning.vertexCount);
  assert(!memcmp(borrowed.firstVertex, owning.firstVertex, sizeof(owning.firstVertex)));
  assert(!memcmp(borrowed.typeVertexCount, owning.typeVertexCount, sizeof(owning.typeVertexCount)));
  assert(!memcmp(borrowed.vertices, owning.vertices, owning.vertexCount * sizeof(*scratch)));
  section_mesh_destroy(&owning);
  world_destroy(&world);
}
int main(void) {
  storage_accounting(1);
  exhausted(0, 1);
  exhausted(1, 0);
  test_pool_reuse();
  test_payload_exhaustion();
  test_staging_exhaustion();
  test_scratch();
  for (int c = 0; c < STORAGE_CATEGORY_COUNT; c++) {
    assert(!storage_stats((StorageCategory)c)->live);
    assert(!storage_stats((StorageCategory)c)->capacity);
  }
  puts("All storage tests passed.");
  return 0;
}
