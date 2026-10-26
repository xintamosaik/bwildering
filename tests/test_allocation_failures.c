#include "../game_logic.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/* Linker wrappers allow deterministic failure at every allocation without
   shipping allocator hooks in the world or renderer. */
void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
static int fail_after = -1;
static int reject_allocation(void) {
  if (fail_after < 0)
    return 0;
  if (fail_after == 0)
    return 1;
  fail_after--;
  return 0;
}
void *__wrap_malloc(size_t size) {
  return reject_allocation() ? NULL : __real_malloc(size);
}
void *__wrap_calloc(size_t count, size_t size) {
  return reject_allocation() ? NULL : __real_calloc(count, size);
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
  Subchunk *section = world_section(&world, -1, 0, 0);
  fail_after = 0;
  assert(!section_mesh_build(&world, -1, 0, 0, &mesh));
  assert(mesh.vertices == original && mesh.vertexCount == 36 && section->dirty);
  fail_after = -1;
  section_mesh_destroy(&mesh);
  /* Reject every allocation in each generator; ownership and content survive. */
  for (int generator = 0; generator < 3; generator++) {
    world_destroy(&world);
    assert(world_init(&world, 32, (Vec3){0}, 1));
    assert(world_set_block(&world, -1, 0, 0, BLOCK_TYPE_GRASS));
    Chunk *chunks = world.chunks;
    Subchunk *originalSection = world_section(&world, -1, 0, 0);
    int successes = 0, failures = 0;
    for (int i = 0; i < 64; i++) {
      fail_after = i;
      int success = generator == 0
          ? create_platform(&world, 17, (Vec3){12, -3, 8}, 2, 1)
          : generator == 1
              ? create_terrain(&world, 17, (Vec3){12, -3, 8}, 2, 1)
              : create_terrain_region(&world, -17, 15, 17, (Vec3){12, -3, 8}, 2, 1);
      fail_after = -1;
      if (success) {
        successes++;
        break;
      }
      failures++;
      assert(world.chunks == chunks && world.voxelCount == 1 && world.sectionCount == 1);
      assert(world_section(&world, -1, 0, 0) == originalSection);
      assert(world_get_block(&world, -1, 0, 0) == BLOCK_TYPE_GRASS);
    }
    assert(failures > 2 && successes == 1);
  }
  world_destroy(&world);
  puts("All allocation failure tests passed.");
  return 0;
}
