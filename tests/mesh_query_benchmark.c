#define _POSIX_C_SOURCE 200809L
#include "../world_generation.h"
#include "../game_logic.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double seconds(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec + t.tv_nsec * 1e-9;
}
#ifdef MESH_QUERY_PROFILE
static struct {
  size_t lookups, steps, samples, queries;
  double lookupTime, sampleTime, queryTime;
} profile;
static TerrainColumn measured_sample(uint32_t seed, int x, int z) {
  double start = seconds();
  TerrainColumn column = terrain_sample_column(seed, x, z);
  profile.samples++;
  profile.sampleTime += seconds() - start;
  return column;
}
#define MESH_BENCH_LOOKUP_BEGIN() double lookupStart = seconds(); profile.lookups++
#define MESH_BENCH_LOOKUP_END() (profile.lookupTime += seconds() - lookupStart)
#define MESH_BENCH_VISIT() (profile.steps++)
#define MESH_BENCH_QUERY_BEGIN() double queryStart = seconds(); profile.queries++
#define MESH_BENCH_QUERY_END() (profile.queryTime += seconds() - queryStart)
#define MESH_BENCH_SAMPLE(seed, x, z) measured_sample(seed, x, z)
#endif
/* Include implementation to compare the frozen baseline using the same private
   helpers and compiler settings. Do not link voxel_world.c a second time. */
#include "../voxel_world.c"
static BlockType baseline_neighbor(const World *world, int x, int y, int z) {
  MESH_BENCH_QUERY_BEGIN();
  BlockType result = world_get_block(world, x, y, z);
  MESH_BENCH_QUERY_END();
  return result;
}
#include "mesh_query_baseline.inc"

typedef int (*Builder)(const World *, int, int, int, SectionMesh *);
static void check_equal(const SectionMesh *a, const SectionMesh *b) {
  assert(a->vertexCount == b->vertexCount);
  assert(!memcmp(a->firstVertex, b->firstVertex, sizeof(a->firstVertex)));
  assert(!memcmp(a->typeVertexCount, b->typeVertexCount, sizeof(a->typeVertexCount)));
  if (a->vertexCount)
    assert(!memcmp(a->vertices, b->vertices, a->vertexCount * sizeof(*a->vertices)));
}
static void measure(const char *scenario, const char *name, Builder build,
                    const World *world, int sy, int repeats) {
  SectionMesh mesh = {0};
  assert(build(world, -1, sy, -1, &mesh)); /* Warm allocator/code. */
#ifdef MESH_QUERY_PROFILE
  memset(&profile, 0, sizeof(profile));
#endif
  double start = seconds();
  for (int n = 0; n < repeats; n++) assert(build(world, -1, sy, -1, &mesh));
  double elapsed = seconds() - start;
  printf("%-20s %-8s %9.3f us/build  %zu vertices", scenario, name,
         elapsed * 1e6 / repeats, mesh.vertexCount);
#ifdef MESH_QUERY_PROFILE
  printf("\n  query %.3f us, lookup %.3f us (%zu calls, %zu links), sample %.3f us (%zu columns)",
         profile.queryTime * 1e6 / repeats, profile.lookupTime * 1e6 / repeats,
         profile.lookups / repeats, profile.steps / repeats,
         profile.sampleTime * 1e6 / repeats, profile.samples / repeats);
#endif
  puts("");
  section_mesh_destroy(&mesh);
}
int main(int argc, char **argv) {
  int repeats = argc > 1 ? atoi(argv[1]) : 100;
  if (repeats < 1) return 2;
  const char *names[] = {"resident surface", "synthetic neighbors", "synthetic target", "edited deep border", "sparse high edit"};
  for (int mode = 0; mode < 5; mode++) {
    World world = {0};
    assert(create_terrain_streamed(&world, 96, (Vec3){0}, 1, 1));
    int sy = voxel_section_coord(terrain_sample_column(1, -8, -8).height - 1);
    if (mode == 0)
      for (int x = -2; x <= 0; x++)
        for (int z = -2; z <= 0; z++) assert(world_materialize_chunk(&world, x, z));
    if (mode == 1 || mode == 3) assert(world_materialize_chunk(&world, -1, -1));
    if (mode == 3) {
      sy = -1;
      assert(world_set_block(&world, 0, -8, -8, BLOCK_TYPE_EMPTY));
    }
    if (mode == 4) {
      sy = 1000;
      assert(world_set_block(&world, -8, sy * SECTION_SIZE + 8, -8, BLOCK_TYPE_SNOW));
    }
    SectionMesh a = {0}, b = {0};
    assert(baseline_mesh_build(&world, -1, sy, -1, &a));
    assert(section_mesh_build(&world, -1, sy, -1, &b));
    check_equal(&a, &b);
    section_mesh_destroy(&a); section_mesh_destroy(&b);
    measure(names[mode], "baseline", baseline_mesh_build, &world, sy, repeats);
    measure(names[mode], "current", section_mesh_build, &world, sy, repeats);
    world_destroy(&world);
  }
  return 0;
}
