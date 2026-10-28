#ifndef WORLD_GENERATION_H
#define WORLD_GENERATION_H

#include "game_logic.h"

enum {
  TERRAIN_BASE_Y = -128,
  TERRAIN_DEEP_ROCK_Y = -64,
  TERRAIN_STONE_TOP_Y = -32,
  TERRAIN_SEA_LEVEL = 0
};

typedef struct {
  int height; /* Exclusive occupied top, including water. */
  int stoneTopY; /* Exclusive solid top, including sand, dirt and grass caps. */
  BlockType material;
  int deepRockY; /* Exclusive dark-stone top. */
  int sandDepth;
  int soilDepth; /* Dirt blocks beneath a single grass block; zero disables grass. */
} TerrainColumn;

enum {
  TERRAIN_RELIEF_HILLS = 1u,
  TERRAIN_RELIEF_MOUNTAINS = 2u,
  TERRAIN_RELIEF_ALL = TERRAIN_RELIEF_HILLS | TERRAIN_RELIEF_MOUNTAINS
};

/* Pure sampling for every signed int X/Z: flat beach 01 and eastern relief 02.
   Fixed experiment placement is seed independent. The default enables both
   hills and mountains; explicit options are immutable per-world descriptors. */
TerrainColumn terrain_sample_column(uint32_t seed, int worldX, int worldZ);
TerrainColumn terrain_sample_column_with_relief(uint32_t seed, int worldX, int worldZ,
                                               unsigned int relief);
TerrainColumn terrain_sample_world_column(const World *world, int worldX, int worldZ);
BlockType terrain_column_block(TerrainColumn column, int y);

/* Creation replaces an initialized world only on success. Storage and chunk
   materialization remain owned by the world implementation. */
int create_platform(World *world, int blocksPerSide, Vec3 center,
                    float blockSize, uint32_t seed);
/* Transactional bounded region generation; bounds/transform as world_init_region. */
int create_terrain_region(World *world, int minX, int minZ, int side,
                          Vec3 origin, float blockSize, uint32_t seed);
int create_terrain_region_with_relief(World *world, int minX, int minZ, int side,
                                     Vec3 origin, float blockSize, uint32_t seed,
                                     unsigned int relief);
/* Experimental beach 01 and relief 02 over the foundation and static ocean. */
int create_terrain(World *world, int blocksPerSide, Vec3 center,
                   float blockSize, uint32_t seed);
/* Same terrain and bounds, generated on demand rather than all at startup. */
int create_terrain_streamed(World *world, int blocksPerSide, Vec3 center,
                            float blockSize, uint32_t seed);
int create_terrain_streamed_with_relief(World *world, int blocksPerSide, Vec3 center,
                                      float blockSize, uint32_t seed, unsigned int relief);

/* Returns a value in [0, upperBound). NULL state or zero bound returns zero
   without advancing state. For positive bounds, a zero state is replaced with
   0x6D2B79F5 before advancing the generator. */
uint32_t random_bounded(uint32_t *state, uint32_t upperBound);

#endif
