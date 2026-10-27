#define _POSIX_C_SOURCE 200809L
#include "../world_generation.h"
#include "../game_logic.h"
#include <assert.h>
#include "../storage.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double seconds(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}
int main(void) {
  const uint32_t seeds[] = {1, 42, 1260202551};
  for (size_t seed = 0; seed < sizeof(seeds) / sizeof(*seeds); seed++) {
    int cx = 0, cz = 0, height = TERRAIN_BASE_Y;
    for (int x = -1024; x <= 1024; x += 64)
      for (int z = -1024; z <= 1024; z += 64) {
        int h = terrain_sample_column(seeds[seed], x, z).height;
        if (h > height) { height = h; cx = x; cz = z; }
      }
    storage_accounting(1);
    World world = {0};
    double generation = 0;
    for (int r = 0; r < 5; r++) {
      double start = seconds();
      assert(create_terrain_region(&world, cx - 64, cz - 64, 128, (Vec3){0}, 1, seeds[seed]));
      generation += seconds() - start;
      if (r < 4) world_destroy(&world);
    }
    size_t dense = 0, uniform = 0, empty = 0;
    size_t requested = 0;
    size_t allocated = 0;
    for (size_t c = 0; c < world.chunkCount; c++)
      for (Subchunk *s = world.chunks[c].sections; s; s = s->next) {
#ifdef LEGACY_SECTION_STORAGE
        dense++;
#else
        dense += s->storage == SECTION_DENSE;
        uniform += s->storage == SECTION_UNIFORM;
        empty += s->storage == SECTION_EMPTY;
#endif
      }
    for (int c = 0; c < STORAGE_CATEGORY_COUNT; c++) {
      requested += storage_stats((StorageCategory)c)->live;
      allocated += storage_stats((StorageCategory)c)->capacity;
    }
    uint64_t checksum = 14695981039346656037ULL;
    for (int x = world.minX; x < world.minX + 128; x++)
      for (int z = world.minZ; z < world.minZ + 128; z++)
        for (int y = TERRAIN_BASE_Y; y < 384; y++)
          checksum = (checksum ^ (uint8_t)world_get_block(&world, x, y, z)) * 1099511628211ULL;
    printf("seed=%u center=%d,%d peak=%d sections=%zu dense=%zu uniform=%zu empty=%zu "
           "node=%zu requested=%zu reserved=%zu generation_ms=%.3f checksum=%llu\n",
           seeds[seed], cx, cz, height, world.sectionCount, dense, uniform, empty,
           sizeof(Subchunk), requested + sizeof(World), allocated + sizeof(World),
           generation * 1000 / 5, (unsigned long long)checksum);
    world_destroy(&world);
  }
  return 0;
}
