#include "world_generation.h"

#include <math.h>

/* EXPERIMENTAL 01: plans/world-generation/experiments/01-small-beach-quadrant.md.
   This is a local profile, not a world-size limit or an archetype registry. */
static const struct {
  int quadrantSide;
  float centerX, centerZ;
  float grassRadius, beachRadius, footholdRadius, shelfRadius;
  int grassTopY, beachTopY, footholdTopY;
  int sandDepth, soilDepth;
} BEACH_EXPERIMENT_01 = {
    .quadrantSide = 512,
    .centerX = 256.0f, .centerZ = 256.0f,
    .grassRadius = 118.0f, .beachRadius = 128.0f,
    .footholdRadius = 132.0f, .shelfRadius = 192.0f,
    .grassTopY = 3, .beachTopY = 2, .footholdTopY = 1,
    .sandDepth = 3, .soilDepth = 2,
};

/* EXPERIMENTAL 02: bounded landform descriptors in local quadrant coordinates.
   The original island is the flat control; only the eastern island gets relief. */
typedef struct { float x, z, radiusX, radiusZ, rise; } ReliefMound;
static const ReliefMound HILLS[] = {
    {214.0f, 244.0f, 50.0f, 50.0f, 8.0f},
    {286.0f, 306.0f, 45.0f, 45.0f, 6.0f},
};
static const ReliefMound MOUNTAIN_BASE = {276.0f, 238.0f, 75.0f, 75.0f, 10.0f};
static const ReliefMound MOUNTAIN_CORE = {276.0f, 238.0f, 68.0f, 40.0f, 30.0f};

static float mound_height(const ReliefMound mound, const float x, const float z) {
  const float dx = (x - mound.x) / mound.radiusX;
  const float dz = (z - mound.z) / mound.radiusZ;
  const float t = fmaxf(0.0f, 1.0f - dx * dx - dz * dz);
  return mound.rise * t * t * t;
}

static float relief_height(const float x, const float z, const unsigned int relief) {
  float hills = 0.0f;
  if (relief & TERRAIN_RELIEF_HILLS)
    for (size_t i = 0; i < sizeof(HILLS) / sizeof(*HILLS); i++)
      hills = fmaxf(hills, mound_height(HILLS[i], x, z));
  const float mountain = relief & TERRAIN_RELIEF_MOUNTAINS ?
      mound_height(MOUNTAIN_BASE, x, z) + mound_height(MOUNTAIN_CORE, x, z) : 0.0f;
  const float dx = x - BEACH_EXPERIMENT_01.centerX;
  const float dz = z - BEACH_EXPERIMENT_01.centerZ;
  const float radius = sqrtf(dx * dx + dz * dz);
  const float t = fminf(1.0f, fmaxf(0.0f, (radius - 100.0f) / 18.0f));
  const float inland = 1.0f - t * t * (3.0f - 2.0f * t);
  return fmaxf(hills, mountain) * inland;
}

static TerrainColumn beach_column(const int worldX, const int worldZ) {
  TerrainColumn column = {.height = TERRAIN_SEA_LEVEL + 1, .stoneTopY = TERRAIN_STONE_TOP_Y,
                          .material = BLOCK_TYPE_WATER, .deepRockY = TERRAIN_DEEP_ROCK_Y};
  /* Bound integer coordinates before conversion; distant and negative queries
     remain the unchanged ocean, including INT_MIN/INT_MAX sampling. */
  if (worldX < 0 || worldZ < 0 || worldX >= BEACH_EXPERIMENT_01.quadrantSide ||
      worldZ >= BEACH_EXPERIMENT_01.quadrantSide) return column;
  const float dx = (float)worldX + 0.5f - BEACH_EXPERIMENT_01.centerX;
  const float dz = (float)worldZ + 0.5f - BEACH_EXPERIMENT_01.centerZ;
  const float distanceSquared = dx * dx + dz * dz;
  if (distanceSquared >= BEACH_EXPERIMENT_01.shelfRadius * BEACH_EXPERIMENT_01.shelfRadius)
    return column;

  const float radius = sqrtf(distanceSquared);
  if (radius < BEACH_EXPERIMENT_01.grassRadius) {
    column.stoneTopY = BEACH_EXPERIMENT_01.grassTopY;
    column.soilDepth = BEACH_EXPERIMENT_01.soilDepth;
  } else {
    if (radius < BEACH_EXPERIMENT_01.beachRadius)
      column.stoneTopY = BEACH_EXPERIMENT_01.beachTopY;
    else if (radius < BEACH_EXPERIMENT_01.footholdRadius)
      column.stoneTopY = BEACH_EXPERIMENT_01.footholdTopY;
    else {
      const float t = (radius - BEACH_EXPERIMENT_01.footholdRadius) /
          (BEACH_EXPERIMENT_01.shelfRadius - BEACH_EXPERIMENT_01.footholdRadius);
      const float smooth = t * t * (3.0f - 2.0f * t);
      column.stoneTopY = (int)floorf((float)BEACH_EXPERIMENT_01.footholdTopY -
          (float)(BEACH_EXPERIMENT_01.footholdTopY - TERRAIN_STONE_TOP_Y) * smooth);
    }
    if (column.stoneTopY > TERRAIN_STONE_TOP_Y) column.sandDepth = BEACH_EXPERIMENT_01.sandDepth;
  }

  if (column.stoneTopY >= column.height) {
    column.height = column.stoneTopY;
    column.material = column.soilDepth ? BLOCK_TYPE_GRASS :
        column.sandDepth ? BLOCK_TYPE_SAND : BLOCK_TYPE_STONE;
  }
  return column;
}

TerrainColumn terrain_sample_column_with_relief(const uint32_t seed, const int worldX,
                                               const int worldZ, const unsigned int relief) {
  (void)seed; /* Fixed experimental placement; no exploration-order RNG. */
  const int eastern = worldX >= 512 && worldX < 1024 && worldZ >= 0 && worldZ < 512;
  const int localX = eastern ? worldX - 512 : worldX;
  TerrainColumn column = beach_column(localX, worldZ);
  if (!eastern || !column.soilDepth || !relief) return column;
  const float x = (float)localX + 0.5f, z = (float)worldZ + 0.5f;
  const float rise = relief_height(x, z, relief);
  column.stoneTopY += (int)floorf(rise + 0.5f);
  column.height = column.stoneTopY;
  /* Evaluate the composed continuous surface across chunk boundaries. Hills
     remain grassy; mountain high ground and steep faces expose the foundation. */
  const float slopeX = 0.5f * (relief_height(x + 1.0f, z, relief) - relief_height(x - 1.0f, z, relief));
  const float slopeZ = 0.5f * (relief_height(x, z + 1.0f, relief) - relief_height(x, z - 1.0f, relief));
  if ((relief & TERRAIN_RELIEF_MOUNTAINS) &&
      (rise >= 24.0f || slopeX * slopeX + slopeZ * slopeZ > 1.21f)) {
    column.soilDepth = 0;
    column.material = BLOCK_TYPE_STONE;
  }
  return column;
}

TerrainColumn terrain_sample_column(const uint32_t seed, const int worldX, const int worldZ) {
  return terrain_sample_column_with_relief(seed, worldX, worldZ, TERRAIN_RELIEF_ALL);
}

TerrainColumn terrain_sample_world_column(const World *const world, const int worldX, const int worldZ) {
  return terrain_sample_column_with_relief(world->terrainSeed, worldX, worldZ, world->terrainRelief);
}

BlockType terrain_column_block(const TerrainColumn column, const int y) {
  if (y < TERRAIN_BASE_Y || y >= column.height) return BLOCK_TYPE_EMPTY;
  if (y < column.deepRockY) return BLOCK_TYPE_DARK_STONE;
  if (y >= column.stoneTopY) return BLOCK_TYPE_WATER;
  const int depth = column.stoneTopY - 1 - y;
  if (depth < column.sandDepth) return BLOCK_TYPE_SAND;
  if (column.soilDepth) {
    if (!depth) return BLOCK_TYPE_GRASS;
    if (depth <= column.soilDepth) return BLOCK_TYPE_DIRT;
  }
  return BLOCK_TYPE_STONE;
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
  return create_terrain_region_with_relief(world, minX, minZ, side, origin, blockSize, seed, TERRAIN_RELIEF_ALL);
}

int create_terrain_region_with_relief(World *const world, const int minX, const int minZ, const int side,
                                     const Vec3 origin, const float blockSize, const uint32_t seed,
                                     const unsigned int relief) {
  World result = {0};
  if ((relief & ~(unsigned int)TERRAIN_RELIEF_ALL) || !world || !world_init_region(&result, minX, minZ, side, origin, blockSize))
    return 0;
  result.proceduralTerrain = 1;
  result.terrainSeed = seed;
  result.terrainRelief = relief;
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
  return create_terrain_streamed_with_relief(world, blocksPerSide, center, blockSize, seed, TERRAIN_RELIEF_ALL);
}

int create_terrain_streamed_with_relief(World *const world, const int blocksPerSide, const Vec3 center,
                                      const float blockSize, const uint32_t seed, const unsigned int relief) {
  World result = {0};
  if ((relief & ~(unsigned int)TERRAIN_RELIEF_ALL) || !world || !world_init(&result, blocksPerSide, center, blockSize))
    return 0;
  result.proceduralTerrain = 1;
  result.terrainSeed = seed;
  result.terrainRelief = relief;
  result.hasOcean = 1;
  result.waterTopY = TERRAIN_SEA_LEVEL + 1;
  world_destroy(world);
  *world = result;
  return 1;
}

