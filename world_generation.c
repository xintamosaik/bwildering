#include "world_generation.h"

TerrainColumn terrain_sample_column(const uint32_t seed, const int worldX, const int worldZ) {
  (void)seed;
  (void)worldX;
  (void)worldZ;
  return (TerrainColumn){.height = TERRAIN_SEA_LEVEL + 1, .stoneTopY = TERRAIN_STONE_TOP_Y,
                         .material = BLOCK_TYPE_WATER,
                         .deepRockY = TERRAIN_DEEP_ROCK_Y};
}

BlockType terrain_column_block(const TerrainColumn column, const int y) {
  if (y < TERRAIN_BASE_Y || y >= column.height) return BLOCK_TYPE_EMPTY;
  if (y < column.deepRockY) return BLOCK_TYPE_DARK_STONE;
  return y < column.stoneTopY ? BLOCK_TYPE_STONE : BLOCK_TYPE_WATER;
}

static uint32_t next_random(uint32_t *const state) {
  uint32_t value = *state;
  value ^= value << 13;
  value ^= value >> 17;
  value ^= value << 5;
  *state = value;
  return value;
}

uint32_t random_bounded(uint32_t *const state, const uint32_t upperBound) {
  if (state == NULL || upperBound == 0u)
    return 0u;

  if (*state == 0u)
    *state = UINT32_C(0x6D2B79F5);

  /* Reject a partial modulo cycle to keep bounded results unbiased. */
  const uint32_t threshold = (0u - upperBound) % upperBound;
  uint32_t value;
  do {
    value = next_random(state);
  } while (value < threshold);

  return value % upperBound;
}

/* Reusable scratch for the legacy shuffled platform fixture. Only the requested
   prefix is touched; generation is single-threaded, with no per-call allocation. */
static uint8_t platformTiles[(size_t)WORLD_MAX_BLOCKS_PER_SIDE * WORLD_MAX_BLOCKS_PER_SIDE];

/* The flat platform retains its global shuffle fixture. */
int create_platform(World *const world, const int blocksPerSide, const Vec3 center, const float blockSize, const uint32_t seed) {
  World result = {0};
  if (world == NULL || !world_init(&result, blocksPerSide, center, blockSize))
    return 0;
  const int count = blocksPerSide * blocksPerSide;
  uint8_t *const tiles = platformTiles;
  for (int i = 0; i < count; i++)
    tiles[i] = (uint8_t)(i % BLOCK_TYPE_COUNT);
  uint32_t state = seed == 0 ? UINT32_C(0x6D2B79F5) : seed;
  for (int i = count - 1; i > 0; i--) {
    const int j = (int)random_bounded(&state, (uint32_t)(i + 1));
    const uint8_t t = tiles[i];
    tiles[i] = tiles[j];
    tiles[j] = t;
  }

  /* Apply the shuffled palette to the platform cells. */
  for (int x = 0; x < blocksPerSide; x++) {
    for (int z = 0; z < blocksPerSide; z++) {
      const int height = 0;
      const BlockType type = (BlockType)tiles[x * blocksPerSide + z];
      for (int y = -1; y < height; y++) {
        if (!world_set_block(&result, result.minX + x, y, result.minZ + z, type)) {
          world_destroy(&result);
          return 0;
        }
      }
    }
  }

  world_destroy(world);
  *world = result;
  return 1;
}

int create_terrain_region(World *const world, const int minX, const int minZ, const int side,
                          const Vec3 origin, const float blockSize, const uint32_t seed) {
  World result = {0};
  if (!world || !world_init_region(&result, minX, minZ, side, origin, blockSize))
    return 0;
  result.proceduralTerrain = 1;
  result.terrainSeed = seed;
  result.hasOcean = 1;
  result.waterTopY = TERRAIN_SEA_LEVEL + 1;
  for (int x = 0; x < result.chunksPerSide; x++)
    for (int z = 0; z < result.chunksPerSide; z++)
      if (!world_materialize_chunk(&result, result.minChunkX + x, result.minChunkZ + z)) {
        world_destroy(&result);
        return 0;
      }

  /* Publish the replacement only after every chunk is ready. */
  result.proceduralTerrain = 0;
  world_destroy(world);
  *world = result;
  return 1;
}

int create_terrain(World *const world, const int blocksPerSide, const Vec3 center, const float blockSize, const uint32_t seed) {
  Vec3 origin = center;
  origin.x -= (float)(blocksPerSide % 2) * blockSize * 0.5f;
  origin.z -= (float)(blocksPerSide % 2) * blockSize * 0.5f;
  return create_terrain_region(world, -(blocksPerSide / 2), -(blocksPerSide / 2), blocksPerSide, origin, blockSize, seed);
}

int create_terrain_streamed(World *const world, const int blocksPerSide, const Vec3 center, const float blockSize, const uint32_t seed) {
  World result = {0};
  if (!world || !world_init(&result, blocksPerSide, center, blockSize))
    return 0;
  result.proceduralTerrain = 1;
  result.terrainSeed = seed;
  result.hasOcean = 1;
  result.waterTopY = TERRAIN_SEA_LEVEL + 1;
  world_destroy(world);
  *world = result;
  return 1;
}

