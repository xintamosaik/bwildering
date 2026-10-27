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

/* Pure voxel sampling for every signed int X/Z. Experimental beach 01 and the
   surrounding ocean are seed independent until shape variation is introduced. */
TerrainColumn terrain_sample_column(uint32_t seed, int worldX, int worldZ);
BlockType terrain_column_block(TerrainColumn column, int y);

/* Creation replaces an initialized world only on success. Storage and chunk
   materialization remain owned by the world implementation. */
int create_platform(World *world, int blocksPerSide, Vec3 center,
                    float blockSize, uint32_t seed);
/* Transactional bounded region generation; bounds/transform as world_init_region. */
int create_terrain_region(World *world, int minX, int minZ, int side,
                          Vec3 origin, float blockSize, uint32_t seed);
/* Experimental beach 01 over the foundation and static ocean. */
int create_terrain(World *world, int blocksPerSide, Vec3 center,
                   float blockSize, uint32_t seed);
/* Same terrain and bounds, generated on demand rather than all at startup. */
int create_terrain_streamed(World *world, int blocksPerSide, Vec3 center,
                            float blockSize, uint32_t seed);

/* Returns a value in [0, upperBound). NULL state or zero bound returns zero
   without advancing state. For positive bounds, a zero state is replaced with
   0x6D2B79F5 before advancing the generator. */
uint32_t random_bounded(uint32_t *state, uint32_t upperBound);

#endif
