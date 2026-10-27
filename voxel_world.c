#include "world_generation.h"
#include "game_logic.h"
#include "storage.h"

#include <math.h>
#include <string.h>

/* Benchmark hooks compile away in normal builds. */
#ifndef MESH_BENCH_LOOKUP_BEGIN
#define MESH_BENCH_LOOKUP_BEGIN() ((void)0)
#define MESH_BENCH_LOOKUP_END() ((void)0)
#define MESH_BENCH_VISIT() ((void)0)
#define MESH_BENCH_QUERY_BEGIN() ((void)0)
#define MESH_BENCH_QUERY_END() ((void)0)
#define MESH_BENCH_SAMPLE(seed, x, z) terrain_sample_column(seed, x, z)
#endif

enum { NODE_PAGE_SLOTS = 128, PAYLOAD_PAGE_SLOTS = 64 };
typedef struct NodePage { struct NodePage *next; Subchunk slots[NODE_PAGE_SLOTS]; } NodePage;
typedef union PayloadSlot { union PayloadSlot *next; uint8_t bytes[SECTION_VOLUME]; } PayloadSlot;
typedef struct PayloadPage { struct PayloadPage *next; PayloadSlot slots[PAYLOAD_PAGE_SLOTS]; } PayloadPage;
struct WorldStorage {
  NodePage *nodes;
  PayloadPage *payloads;
  Subchunk *freeNodes;
  PayloadSlot *freePayloads;
  size_t nodeLimit, payloadLimit, nodeLive, payloadLive, nodeSlots, payloadSlots;
};
int world_storage_limits(World *const world, const size_t nodes, const size_t payloads) {
  if (!world || !world->storage || world->storage->nodes || world->storage->payloads) return 0;
  world->storage->nodeLimit = nodes;
  world->storage->payloadLimit = payloads;
  return 1;
}
static Subchunk *node_acquire(struct WorldStorage *const pool) {
  if (!storage_reserve(STORAGE_NODE, sizeof(Subchunk), pool->freeNodes != NULL)) return NULL;
  if (pool->nodeLive >= pool->nodeLimit) { storage_exhausted(STORAGE_NODE); goto fail; }
  if (!pool->freeNodes) {
    NodePage *const page = storage_backing_alloc(STORAGE_NODE, sizeof(*page));
    if (!page) goto fail;
    page->next = pool->nodes;
    pool->nodes = page;
    const size_t remaining = pool->nodeLimit - pool->nodeSlots;
    const size_t count = remaining < NODE_PAGE_SLOTS ? remaining : NODE_PAGE_SLOTS;
    for (size_t i = 0; i < count; i++) {
      page->slots[i].next = pool->freeNodes;
      pool->freeNodes = &page->slots[i];
    }
    pool->nodeSlots += count;
  }
  Subchunk *const s = pool->freeNodes;
  pool->freeNodes = s->next;
  *s = (Subchunk){.pool = pool};
  pool->nodeLive++;
  return s;
fail:
  storage_release(STORAGE_NODE, sizeof(Subchunk));
  return NULL;
}
static uint8_t *payload_acquire(struct WorldStorage *const pool) {
  if (!storage_reserve(STORAGE_PAYLOAD, SECTION_VOLUME, pool->freePayloads != NULL)) return NULL;
  if (pool->payloadLive >= pool->payloadLimit) { storage_exhausted(STORAGE_PAYLOAD); goto fail; }
  if (!pool->freePayloads) {
    PayloadPage *const page = storage_backing_alloc(STORAGE_PAYLOAD, sizeof(*page));
    if (!page) goto fail;
    page->next = pool->payloads;
    pool->payloads = page;
    const size_t remaining = pool->payloadLimit - pool->payloadSlots;
    const size_t count = remaining < PAYLOAD_PAGE_SLOTS ? remaining : PAYLOAD_PAGE_SLOTS;
    for (size_t i = 0; i < count; i++) {
      page->slots[i].next = pool->freePayloads;
      pool->freePayloads = &page->slots[i];
    }
    pool->payloadSlots += count;
  }
  PayloadSlot *const slot = pool->freePayloads;
  pool->freePayloads = slot->next;
  pool->payloadLive++;
  return slot->bytes;
fail:
  storage_release(STORAGE_PAYLOAD, SECTION_VOLUME);
  return NULL;
}
static void payload_release(Subchunk *const section) {
  if (!section->blocks) return;
  struct WorldStorage *const pool = section->pool;
  PayloadSlot *const slot = (PayloadSlot *)section->blocks;
  slot->next = pool->freePayloads;
  pool->freePayloads = slot;
  pool->payloadLive--;
  storage_release(STORAGE_PAYLOAD, SECTION_VOLUME);
  section->blocks = NULL;
}

int voxel_floor_div(const int coordinate, const int divisor) {
  return coordinate / divisor - (coordinate % divisor < 0);
}

int voxel_floor_mod(const int coordinate, const int divisor) {
  const int remainder = coordinate % divisor;
  return remainder < 0 ? remainder + divisor : remainder;
}

int voxel_section_coord(const int coordinate) {
  return voxel_floor_div(coordinate, SECTION_SIZE);
}

int voxel_local_coord(const int coordinate) {
  return voxel_floor_mod(coordinate, SECTION_SIZE);
}

int block_is_solid(const BlockType type) {
  return type >= BLOCK_TYPE_GRASS && type < BLOCK_TYPE_COUNT;
}

int block_face_visible(const BlockType type, const BlockType neighbor) {
  /* All current materials are opaque, including the water placeholder. */
  return block_is_solid(type) && !block_is_solid(neighbor);
}

static void section_destroy(Subchunk *const section) {
  if (!section) return;
  payload_release(section);
  struct WorldStorage *const pool = section->pool;
  section->next = pool->freeNodes;
  pool->freeNodes = section;
  pool->nodeLive--;
  storage_release(STORAGE_NODE, sizeof(*section));
}

/* Compact without allocating. Mixed full sections usually differ early. */
static void section_compact(Subchunk *const section) {
  if (section->storage != SECTION_DENSE) return;
  uint8_t material = BLOCK_TYPE_EMPTY;
  if (section->voxelCount) {
    if (section->voxelCount != SECTION_VOLUME) return;
    material = section->blocks[0];
    for (size_t i = 1; i < SECTION_VOLUME; i++)
      if (section->blocks[i] != material) return;
  }

  payload_release(section);
  section->blocks = NULL;
  section->material = material;
  section->storage = section->voxelCount ? SECTION_UNIFORM : SECTION_EMPTY;
}

/* Allocate first; callers commit ownership/counts only after success. */
static int section_expand(Subchunk *const section) {
  if (section->storage == SECTION_DENSE) return 1;
  uint8_t *const blocks = payload_acquire(section->pool);
  if (!blocks) return 0;
  memset(blocks, section->storage == SECTION_EMPTY ? BLOCK_TYPE_EMPTY : section->material,
         SECTION_VOLUME);
  section->blocks = blocks;
  section->storage = SECTION_DENSE;
  return 1;
}

void world_destroy(World *const world) {
  if (!world)
    return;
  for (size_t c = 0; c < world->chunkCount; c++)
    {
      Subchunk *section = world->chunks[c].sections;
      while (section) { Subchunk *const next = section->next; section_destroy(section); section = next; }
    }
  storage_free(STORAGE_WORLD, world->edits, WORLD_EDIT_CAPACITY * sizeof(*world->edits));
  storage_free(STORAGE_WORLD, world->chunks, world->chunkCount * sizeof(*world->chunks));
  if (world->storage) {
    NodePage *n = world->storage->nodes;
    while (n) { NodePage *const next = n->next; storage_backing_free(STORAGE_NODE, n, sizeof(*n)); n = next; }
    PayloadPage *p = world->storage->payloads;
    while (p) { PayloadPage *const next = p->next; storage_backing_free(STORAGE_PAYLOAD, p, sizeof(*p)); p = next; }
    storage_free(STORAGE_WORLD, world->storage, sizeof(*world->storage));
  }
  *world = (World){0};
}

int world_init(World *const world, const int side, const Vec3 center, const float blockSize) {
  Vec3 origin = center;
  origin.x -= (float)(side % 2) * blockSize * 0.5f;
  origin.z -= (float)(side % 2) * blockSize * 0.5f;
  return world_init_region(world, -(side / 2), -(side / 2), side, origin, blockSize);
}

int world_init_region(World *const world, const int minX, const int minZ, const int side, const Vec3 origin,
                       const float blockSize) {
  /* Validate in wide arithmetic before endpoint, chunk or neighbour arithmetic. */
  if (!world || side <= 0 || side > WORLD_MAX_BLOCKS_PER_SIDE ||
      minX < WORLD_COORD_MIN || minZ < WORLD_COORD_MIN ||
      (int64_t)minX + side - 1 > WORLD_COORD_MAX ||
      (int64_t)minZ + side - 1 > WORLD_COORD_MAX || !isfinite(blockSize) ||
      blockSize < 0.001f || !isfinite(origin.x) || !isfinite(origin.y) || !isfinite(origin.z))
    return 0;

  /* Keep each block at least one float ULP wide throughout the bounded world. */
  const float origins[3] = {origin.x, origin.y, origin.z};
  const float extents[3] = {
      fmaxf(fabsf((float)minX), fabsf((float)(minX + side))),
      WORLD_SECTION_COUNT * SECTION_SIZE,
      fmaxf(fabsf((float)minZ), fabsf((float)(minZ + side)))};
  for (int axis = 0; axis < 3; axis++) {
    const float limit = fabsf(origins[axis]) + extents[axis] * blockSize;
    if (!isfinite(limit) || nextafterf(limit, INFINITY) - limit > blockSize)
      return 0;
  }

  World result = {0};
  result.minX = minX;
  result.minZ = minZ;
  result.minChunkX = voxel_section_coord(minX);
  result.minChunkZ = voxel_section_coord(minZ);
  const int chunksX = voxel_section_coord(minX + side - 1) - result.minChunkX + 1;
  const int chunksZ = voxel_section_coord(minZ + side - 1) - result.minChunkZ + 1;
  /* Pad the existing square chunk grid if the axes have different alignment. */
  result.chunksPerSide = chunksX > chunksZ ? chunksX : chunksZ;
  result.chunkCount = (size_t)result.chunksPerSide * result.chunksPerSide;
  result.blocksPerSide = side;
  result.blockSize = blockSize;
  result.origin = origin;
  result.chunks = storage_alloc(STORAGE_WORLD, result.chunkCount * sizeof(*result.chunks));
  if (!result.chunks)
    return 0;
  result.storage = storage_alloc(STORAGE_WORLD, sizeof(*result.storage));
  if (!result.storage) {
    storage_free(STORAGE_WORLD, result.chunks, result.chunkCount * sizeof(*result.chunks));
    return 0;
  }
  const size_t limit = result.chunkCount * WORLD_SECTION_COUNT + WORLD_EDIT_CAPACITY + WORLD_SECTION_COUNT;
  *result.storage = (struct WorldStorage){.nodeLimit = limit, .payloadLimit = limit};
  memset(result.chunks, 0, result.chunkCount * sizeof(*result.chunks));
  for (int x = 0; x < result.chunksPerSide; x++)
    for (int z = 0; z < result.chunksPerSide; z++) {
      Chunk *const chunk = &result.chunks[x * result.chunksPerSide + z];
      chunk->x = result.minChunkX + x;
      chunk->z = result.minChunkZ + z;
    }

  world_destroy(world);
  *world = result;
  return 1;
}

static int find_chunk_index(const World *const world, const int x, const int z, size_t *const index) {
  if (!world || !world->chunks || x < world->minChunkX || z < world->minChunkZ ||
      x >= world->minChunkX + world->chunksPerSide ||
      z >= world->minChunkZ + world->chunksPerSide)
    return 0;
  *index = (size_t)(x - world->minChunkX) * world->chunksPerSide + z - world->minChunkZ;
  return 1;
}

static const Chunk *find_chunk(const World *const world, const int x, const int z) {
  size_t index;
  return find_chunk_index(world, x, z, &index) ? &world->chunks[index] : NULL;
}

static Chunk *find_chunk_mut(const World *const world, const int x, const int z) {
  size_t index;
  return find_chunk_index(world, x, z, &index) ? &world->chunks[index] : NULL;
}

static Subchunk **section_slot(Chunk *const chunk, const int y) {
  Subchunk **slot = &chunk->sections;
  while (*slot && (*slot)->sectionY < y) slot = &(*slot)->next;
  return slot;
}

const Subchunk *world_section(const World *const world, const int chunkX, const int sectionY, const int chunkZ) {
  MESH_BENCH_LOOKUP_BEGIN();
  const Chunk *const chunk = find_chunk(world, chunkX, chunkZ);
  const Subchunk *s = chunk ? chunk->sections : NULL;
  while (s && s->sectionY < sectionY) {
    MESH_BENCH_VISIT();
    s = s->next;
  }
  MESH_BENCH_LOOKUP_END();
  return s && s->sectionY == sectionY ? s : NULL;
}

Subchunk *world_section_mut(const World *const world, const int chunkX, const int sectionY, const int chunkZ) {
  return (Subchunk *)world_section(world, chunkX, sectionY, chunkZ);
}

static int in_bounds(const World *const world, const int x, const int y, const int z) {
  return world && world->chunks && x >= world->minX && z >= world->minZ &&
         x < world->minX + world->blocksPerSide &&
         z < world->minZ + world->blocksPerSide &&
         y >= WORLD_COORD_MIN && y <= WORLD_COORD_MAX;
}

static int block_index(const int x, const int y, const int z) {
  return (voxel_local_coord(x) * SECTION_SIZE + voxel_local_coord(y)) * SECTION_SIZE +
         voxel_local_coord(z);
}

static const VoxelEdit *find_edit(const World *const world, const Chunk *const chunk,
                                   const int x, const int y, const int z) {
  for (uint32_t i = chunk->editHead; i; i = world->edits[i - 1].next) {
    const VoxelEdit *const edit = &world->edits[i - 1];
    if (edit->x == x && edit->y == y && edit->z == z) return edit;
  }
  return NULL;
}

static BlockType baseline_block(const World *const world, const int x, const int y, const int z) {
  return world->proceduralTerrain && y >= TERRAIN_BASE_Y &&
      y < (WORLD_MIN_SECTION_Y + WORLD_SECTION_COUNT) * SECTION_SIZE ?
      terrain_column_block(MESH_BENCH_SAMPLE(world->terrainSeed, x, z), y) : BLOCK_TYPE_EMPTY;
}

static void include_resident_chunk(World *const world, const int cx, const int cz) {
  if (!world->hasMaterializedBounds) {
    world->minMaterializedChunkX = world->maxMaterializedChunkX = cx;
    world->minMaterializedChunkZ = world->maxMaterializedChunkZ = cz;
    world->hasMaterializedBounds = 1;
  } else {
    if (cx < world->minMaterializedChunkX) world->minMaterializedChunkX = cx;
    if (cx > world->maxMaterializedChunkX) world->maxMaterializedChunkX = cx;
    if (cz < world->minMaterializedChunkZ) world->minMaterializedChunkZ = cz;
    if (cz > world->maxMaterializedChunkZ) world->maxMaterializedChunkZ = cz;
  }
}

BlockType world_get_block(const World *const world, const int x, const int y, const int z) {
  if (!in_bounds(world, x, y, z))
    return BLOCK_TYPE_EMPTY;
  const Subchunk *const section = world_section(world, voxel_section_coord(x), voxel_section_coord(y),
                                    voxel_section_coord(z));
  if (section)
    return section_block(section, block_index(x, y, z));

  /* Missing cache data reads from authoritative overlay before the seed. */
  const Chunk *const chunk = find_chunk(world, voxel_section_coord(x), voxel_section_coord(z));
  const VoxelEdit *const edit = find_edit(world, chunk, x, y, z);
  if (edit) return (BlockType)edit->material;
  if (world->proceduralTerrain && !chunk->terrainReady)
    return baseline_block(world, x, y, z);
  return BLOCK_TYPE_EMPTY;
}

static Subchunk *empty_section(const World *const world) { return node_acquire(world->storage); }

static Subchunk *terrain_section(const World *const world, const int cx, const int sy, const int cz,
                                 const TerrainColumn columns[const SECTION_SIZE][SECTION_SIZE]) {
  Subchunk *const section = node_acquire(world->storage);
  if (!section)
    return NULL;
  *section = (Subchunk){.pool = world->storage, .storage = SECTION_EMPTY, .material = BLOCK_TYPE_EMPTY,
      .dirty = 1, .meshCandidate = 1, .sectionY = sy};

  /* Stack staging avoids allocating a dense payload for homogeneous terrain. */
  uint8_t blocks[SECTION_VOLUME];
  memset(blocks, BLOCK_TYPE_EMPTY, sizeof(blocks));
  for (int x = 0; x < SECTION_SIZE; x++)
    for (int z = 0; z < SECTION_SIZE; z++) {
      const int wx = cx * SECTION_SIZE + x, wz = cz * SECTION_SIZE + z;
      if (wx < world->minX || wx >= world->minX + world->blocksPerSide ||
          wz < world->minZ || wz >= world->minZ + world->blocksPerSide)
        continue;
      const TerrainColumn column = columns ? columns[x][z] :
          MESH_BENCH_SAMPLE(world->terrainSeed, wx, wz);
      for (int y = 0; y < SECTION_SIZE; y++) {
        const int wy = sy * SECTION_SIZE + y;
        const BlockType type = wy >= TERRAIN_BASE_Y &&
            wy < (WORLD_MIN_SECTION_Y + WORLD_SECTION_COUNT) * SECTION_SIZE ?
            terrain_column_block(column, wy) : BLOCK_TYPE_EMPTY;
        blocks[(x * SECTION_SIZE + y) * SECTION_SIZE + z] = (uint8_t)type;
        section->voxelCount += type != BLOCK_TYPE_EMPTY;
      }
    }

  const Chunk *const chunk = find_chunk(world, cx, cz);
  for (uint32_t i = chunk->editHead; i; i = world->edits[i - 1].next) {
    const VoxelEdit *const edit = &world->edits[i - 1];
    if (voxel_section_coord(edit->y) != sy) continue;
    const int index = block_index(edit->x, edit->y, edit->z);
    section->voxelCount -= blocks[index] != BLOCK_TYPE_EMPTY;
    blocks[index] = edit->material;
    section->voxelCount += blocks[index] != BLOCK_TYPE_EMPTY;
  }

  /* Keep homogeneous sections compact; only mixed sections need a payload. */
  const uint8_t material = blocks[0];
  int uniform = 1;
  for (size_t i = 1; i < SECTION_VOLUME; i++)
    if (blocks[i] != material) { uniform = 0; break; }
  if (uniform) {
    section->storage = material == BLOCK_TYPE_EMPTY ? SECTION_EMPTY : SECTION_UNIFORM;
    section->material = material;
  } else {
    if (!section_expand(section)) { section_destroy(section); return NULL; }
    memcpy(section->blocks, blocks, sizeof(blocks));
  }
  return section;
}

int world_materialize_chunk(World *const world, const int chunkX, const int chunkZ) {
  Chunk *const chunk = find_chunk_mut(world, chunkX, chunkZ);
  if (!chunk || !world->proceduralTerrain)
    return 0;
  if (chunk->terrainReady)
    return 1;

  /* Sample each column once for all vertical sections in this chunk. */
  TerrainColumn columns[SECTION_SIZE][SECTION_SIZE];
  int highest = TERRAIN_BASE_Y;
  for (int x = 0; x < SECTION_SIZE; x++)
    for (int z = 0; z < SECTION_SIZE; z++) {
      const int wx = chunkX * SECTION_SIZE + x, wz = chunkZ * SECTION_SIZE + z;
      columns[x][z] = (TerrainColumn){.height = TERRAIN_BASE_Y, .material = BLOCK_TYPE_STONE};
      if (wx >= world->minX && wx < world->minX + world->blocksPerSide &&
          wz >= world->minZ && wz < world->minZ + world->blocksPerSide) {
        columns[x][z] = terrain_sample_column(world->terrainSeed, wx, wz);
        if (columns[x][z].height > highest)
          highest = columns[x][z].height;
      }
    }

  /* Include the border heights when identifying potentially exposed sections. */
  int lowest = highest;
  int edge = 0;
  for (int x = -1; x <= SECTION_SIZE; x++)
    for (int z = -1; z <= SECTION_SIZE; z++) {
      const int wx = chunkX * SECTION_SIZE + x, wz = chunkZ * SECTION_SIZE + z;
      if (wx < world->minX || wx >= world->minX + world->blocksPerSide ||
          wz < world->minZ || wz >= world->minZ + world->blocksPerSide) { edge = 1; continue; }
      const int h = x >= 0 && x < SECTION_SIZE && z >= 0 && z < SECTION_SIZE ?
          columns[x][z].height : terrain_sample_column(world->terrainSeed, wx, wz).height;
      if (h < lowest) lowest = h;
    }

  /* Stage missing sections so a failed allocation leaves the chunk intact. */
  Subchunk *pending[WORLD_SECTION_COUNT] = {0};
  for (int s = 0; s < WORLD_SECTION_COUNT; s++) {
    const int sy = WORLD_MIN_SECTION_Y + s;
    if (world_section(world, chunkX, sy, chunkZ) || sy * SECTION_SIZE >= highest ||
        (sy + 1) * SECTION_SIZE <= TERRAIN_BASE_Y)
      continue;
    pending[s] = terrain_section(world, chunkX, sy, chunkZ, columns);
    if (pending[s]) pending[s]->meshCandidate = edge || sy == WORLD_MIN_SECTION_Y ||
        (sy + 1) * SECTION_SIZE >= lowest;
    if (!pending[s]) {
      for (int j = 0; j < WORLD_SECTION_COUNT; j++) section_destroy(pending[j]);
      return 0;
    }
  }

  /* Sparse edits outside generated elevations need their own staged sections. */
  Chunk extra = {0};
  for (uint32_t i = chunk->editHead; i; i = world->edits[i - 1].next) {
    const int sy = voxel_section_coord(world->edits[i - 1].y);
    Subchunk **const slot = section_slot(&extra, sy);
    if (world_section(world, chunkX, sy, chunkZ) || (*slot && (*slot)->sectionY == sy) ||
        (sy >= WORLD_MIN_SECTION_Y && sy < WORLD_MIN_SECTION_Y + WORLD_SECTION_COUNT &&
         pending[sy - WORLD_MIN_SECTION_Y])) continue;
    Subchunk *const section = terrain_section(world, chunkX, sy, chunkZ, columns);
    if (!section) {
      for (int j = 0; j < WORLD_SECTION_COUNT; j++) section_destroy(pending[j]);
      while (extra.sections) {
        Subchunk *const next = extra.sections->next;
        section_destroy(extra.sections);
        extra.sections = next;
      }
      return 0;
    }
    section->next = *slot;
    *slot = section;
  }

  /* Commit the staged sections and their counts together. */
  while (extra.sections) {
    Subchunk *const section = extra.sections;
    extra.sections = section->next;
    Subchunk **const slot = section_slot(chunk, section->sectionY);
    section->next = *slot;
    *slot = section;
    world->sectionCount++;
    world->voxelCount += section->voxelCount;
  }
  for (int s = 0; s < WORLD_SECTION_COUNT; s++)
    if (pending[s]) {
      Subchunk **const slot = section_slot(chunk, pending[s]->sectionY);
      pending[s]->next = *slot;
      *slot = pending[s];
      world->sectionCount++;
      world->voxelCount += pending[s]->voxelCount;
    }

  chunk->terrainReady = 1;
  include_resident_chunk(world, chunkX, chunkZ);
  return 1;
}

void world_release_chunk(World *const world, const int chunkX, const int chunkZ) {
  Chunk *const chunk = find_chunk_mut(world, chunkX, chunkZ);
  if (!chunk || !world->proceduralTerrain)
    return;
  Subchunk *section = chunk->sections;
  while (section) {
    Subchunk *const next = section->next;
    world->voxelCount -= section->voxelCount;
    world->sectionCount--;
    section_destroy(section);
    section = next;
  }
  chunk->sections = NULL;
  chunk->terrainReady = 0;
}

size_t world_release_chunks_outside(World *const world, const int minX, const int maxX, const int minZ, const int maxZ) {
  if (!world || !world->proceduralTerrain || !world->hasMaterializedBounds) return 0;
  const int oldMinX = world->minMaterializedChunkX, oldMaxX = world->maxMaterializedChunkX;
  const int oldMinZ = world->minMaterializedChunkZ, oldMaxZ = world->maxMaterializedChunkZ;
  if (oldMinX >= minX && oldMaxX <= maxX && oldMinZ >= minZ && oldMaxZ <= maxZ) return 0;
  size_t released = 0;
  /* Visit only departing strips of the conservative materialized envelope. */
  for (int x = oldMinX; x <= oldMaxX; x++) {
    const int insideX = x >= minX && x <= maxX;
    for (int z = oldMinZ; z <= oldMaxZ; z++) {
      if (insideX && z >= minZ && z <= maxZ) {
        z = maxZ < oldMaxZ ? maxZ : oldMaxZ;
        continue;
      }
      const Chunk *const chunk = find_chunk(world, x, z);
      if (chunk && (chunk->terrainReady || chunk->sections)) {
        world_release_chunk(world, x, z);
        released++;
      }
    }
  }

  world->minMaterializedChunkX = oldMinX > minX ? oldMinX : minX;
  world->maxMaterializedChunkX = oldMaxX < maxX ? oldMaxX : maxX;
  world->minMaterializedChunkZ = oldMinZ > minZ ? oldMinZ : minZ;
  world->maxMaterializedChunkZ = oldMaxZ < maxZ ? oldMaxZ : maxZ;
  world->hasMaterializedBounds = world->minMaterializedChunkX <= world->maxMaterializedChunkX &&
      world->minMaterializedChunkZ <= world->maxMaterializedChunkZ;
  return released;
}

int world_set_block(World *const world, const int x, const int y, const int z, const BlockType type) {
  if (!in_bounds(world, x, y, z) || (!block_is_solid(type) && type != BLOCK_TYPE_EMPTY))
    return 0;

  /* Generation bounds are independent of edit safety. Test float spacing at
     this edit's height; reads need only integer bounds and existing storage. */
  const float limit = fabsf(world->origin.y) + (fabsf((float)y) + 1) * world->blockSize;
  if (!isfinite(limit) || nextafterf(limit, INFINITY) - limit > world->blockSize)
    return 0;
  const int cx = voxel_section_coord(x), sy = voxel_section_coord(y),
      cz = voxel_section_coord(z);
  Chunk *const chunk = find_chunk_mut(world, cx, cz);
  Subchunk **const slot = section_slot(chunk, sy);
  const int missing = !*slot || (*slot)->sectionY != sy;
  const int index = block_index(x, y, z);
  if ((missing ? world_get_block(world, x, y, z) : section_block(*slot, index)) == type)
    return 1;

  const BlockType baseline = baseline_block(world, x, y, z);
  uint32_t *editSlot = &chunk->editHead;
  while (*editSlot) {
    const VoxelEdit *const edit = &world->edits[*editSlot - 1];
    if (edit->x == x && edit->y == y && edit->z == z) break;
    editSlot = &world->edits[*editSlot - 1].next;
  }
  const int insert = world->proceduralTerrain && type != baseline && !*editSlot;
  if (insert && world->editCount == WORLD_EDIT_CAPACITY) return 0;
  /* Reserve the whole arena once, without publishing ownership on failure. */
  VoxelEdit *const arena = insert && !world->edits ?
      storage_alloc(STORAGE_WORLD, WORLD_EDIT_CAPACITY * sizeof(*arena)) : NULL;
  if (insert && !world->edits && !arena) return 0;

  /* Prepare writable storage before linking a new section into the world. */
  Subchunk *const section = missing ?
      (world->proceduralTerrain && !chunk->terrainReady ?
       terrain_section(world, cx, sy, cz, NULL) : empty_section(world)) : *slot;
  if (!section) { storage_free(STORAGE_WORLD, arena, WORLD_EDIT_CAPACITY * sizeof(*arena)); return 0; }
  if (missing && section->storage == SECTION_EMPTY) section->material = BLOCK_TYPE_EMPTY;
  const BlockType old = section_block(section, index);
  if (!section_expand(section)) {
    if (missing) section_destroy(section);
    storage_free(STORAGE_WORLD, arena, WORLD_EDIT_CAPACITY * sizeof(*arena));
    return 0;
  }
  if (missing) {
    section->sectionY = sy;
    section->next = *slot;
    *slot = section;
    world->sectionCount++;
    world->voxelCount += section->voxelCount;
  }

  if (old == BLOCK_TYPE_EMPTY) {
    section->voxelCount++;
    world->voxelCount++;
  }
  if (type == BLOCK_TYPE_EMPTY) {
    section->voxelCount--;
    world->voxelCount--;
  }
  section->meshCandidate = 1;
  section->blocks[index] = (uint8_t)type;
  section_compact(section);
  section->dirty = 1;
  section->revision++;
  if (world->proceduralTerrain) {
    if (arena) world->edits = arena;
    if (insert) {
      const uint32_t entry = world->editFree ? world->editFree : ++world->editUsed;
      if (world->editFree) world->editFree = world->edits[entry - 1].next;
      world->edits[entry - 1] = (VoxelEdit){.x = x, .y = y, .z = z,
          .material = (uint8_t)type};
      *editSlot = entry;
      world->editCount++;
    } else if (*editSlot) {
      VoxelEdit *const edit = &world->edits[*editSlot - 1];
      if (type == baseline) {
        const uint32_t entry = *editSlot;
        *editSlot = edit->next;
        edit->next = world->editFree;
        world->editFree = entry;
        world->editCount--;
      } else edit->material = (uint8_t)type;
    }
    chunk->edited = chunk->editHead != 0;
    include_resident_chunk(world, cx, cz);
  }

  /* Only face neighbors depend on the changed voxel. Keep empty sections as
     tombstones so the renderer can retire their previous GPU meshes. */
  const int local[3] = {voxel_local_coord(x), voxel_local_coord(y), voxel_local_coord(z)};
  for (int d = 0; d < 6; d++) {
    const int dz[6] = {0, 0, 0, 0, -1, 1};
    const int dy[6] = {0, 0, -1, 1, 0, 0};
    const int dx[6] = {-1, 1, 0, 0, 0, 0};
    if ((d == 0 && local[0] != 0) || (d == 1 && local[0] != 15) ||
        (d == 2 && local[1] != 0) || (d == 3 && local[1] != 15) ||
        (d == 4 && local[2] != 0) || (d == 5 && local[2] != 15))
      continue;
    Subchunk *const neighbor = world_section_mut(world, cx + dx[d], sy + dy[d], cz + dz[d]);
    if (neighbor) {
      neighbor->dirty = 1;
      neighbor->revision++;
      neighbor->meshCandidate = 1;
    }
  }
  return 1;
}

float world_surface_height(const World *const world, const float x, const float z) {
  if (!world || !world->chunks || !isfinite(x) || !isfinite(z))
    return NAN;
  const float vx = (x - world->origin.x) / world->blockSize;
  const float vz = (z - world->origin.z) / world->blockSize;
  if (vx < (float)world->minX || vx > (float)(world->minX + world->blocksPerSide) || vz < (float)world->minZ ||
      vz > (float)(world->minZ + world->blocksPerSide))
    return NAN;
  const int ix = vx == (float)(world->minX + world->blocksPerSide)
               ? world->minX + world->blocksPerSide - 1
               : (int)floorf(vx);
  const int iz = vz == (float)(world->minZ + world->blocksPerSide)
               ? world->minZ + world->blocksPerSide - 1
               : (int)floorf(vz);
  const Chunk *const chunk = find_chunk(world, voxel_section_coord(ix), voxel_section_coord(iz));
  int top = WORLD_COORD_MIN;
  int found = 0;

  /* Start with the generated surface, accounting for explicit removals. */
  if (world->proceduralTerrain && !chunk->terrainReady) {
    const TerrainColumn column = terrain_sample_column(world->terrainSeed, ix, iz);
    int y = column.height - 1;
    while (y >= TERRAIN_BASE_Y) {
      if (block_is_solid(world_get_block(world, ix, y, iz))) break;
      y--;
    }
    if (y >= TERRAIN_BASE_Y) { top = y + 1; found = 1; }
  }

  for (uint32_t i = chunk->editHead; i; i = world->edits[i - 1].next) {
    const VoxelEdit *const edit = &world->edits[i - 1];
    if (edit->x == ix && edit->z == iz && block_is_solid((BlockType)edit->material) &&
        (!found || edit->y + 1 > top)) {
      top = edit->y + 1;
      found = 1;
    }
  }

  /* Stored sections can contain edits above the generated surface. */
  for (const Subchunk *s = chunk->sections; s; s = s->next) {
    if (found && (s->sectionY + 1) * SECTION_SIZE <= top) continue;
    for (int ly = SECTION_SIZE - 1; ly >= 0; ly--)
      if (block_is_solid(section_block(s, block_index(ix, ly, iz)))) {
        const int h = s->sectionY * SECTION_SIZE + ly + 1;
        if (!found || h > top) top = h;
        found = 1;
        break;
      }
  }
  return found ? world->origin.y + (float)top * world->blockSize : NAN;
}

void section_mesh_destroy(SectionMesh *const mesh) {
  if (!mesh)
    return;
  storage_free(STORAGE_MESH, mesh->vertices, mesh->vertexCount * sizeof(*mesh->vertices));
  *mesh = (SectionMesh){0};
}

/* Build-local cache: only face neighbours are read; no persistent invalidation
   or ownership is needed. Allocated sections (including empty edit markers)
   always take precedence over procedural terrain. */
typedef struct {
  const Subchunk *sections[6];
  const Chunk *chunks[6];
  TerrainColumn columns[SECTION_SIZE + 2][SECTION_SIZE + 2];
  uint8_t sampled[SECTION_SIZE + 2][SECTION_SIZE + 2];
} MeshNeighbors;

static BlockType mesh_boundary_block(const World *const world, MeshNeighbors *const cache,
                                     const int d, const int cx, const int sy, const int cz,
                                     const int x, const int y, const int z) {
  const int wx = cx * SECTION_SIZE + x, wy = sy * SECTION_SIZE + y,
      wz = cz * SECTION_SIZE + z;
  if (!in_bounds(world, wx, wy, wz)) return BLOCK_TYPE_EMPTY;
  if (cache->sections[d])
    return section_block(cache->sections[d], block_index(x, y, z));
  if (cache->chunks[d]) {
    const VoxelEdit *const edit = find_edit(world, cache->chunks[d], wx, wy, wz);
    if (edit) return (BlockType)edit->material;
  }
  if (!world->proceduralTerrain || !cache->chunks[d] || cache->chunks[d]->terrainReady ||
      wy < TERRAIN_BASE_Y || wy >= (WORLD_MIN_SECTION_Y + WORLD_SECTION_COUNT) * SECTION_SIZE)
    return BLOCK_TYPE_EMPTY;
  if (!cache->sampled[x + 1][z + 1]) {
    cache->columns[x + 1][z + 1] = MESH_BENCH_SAMPLE(world->terrainSeed, wx, wz);
    cache->sampled[x + 1][z + 1] = 1;
  }
  return terrain_column_block(cache->columns[x + 1][z + 1], wy);
}

static int mesh_build(const World *const world, const int cx, const int sy, const int cz,
                      SectionMesh *const mesh, SurfaceVertex *const scratch,
                      const size_t capacity, const int borrowed) {
  const Chunk *const chunk = find_chunk(world, cx, cz);
  if (!mesh || !chunk || sy < voxel_section_coord(WORLD_COORD_MIN) ||
      sy > voxel_section_coord(WORLD_COORD_MAX))
    return 0;
  SectionMesh result = {0};
  const Subchunk *section = world_section(world, cx, sy, cz);
  Subchunk *temporary = NULL;
  if (!section && world->proceduralTerrain && !chunk->terrainReady) {
    temporary = terrain_section(world, cx, sy, cz, NULL);
    if (!temporary)
      return 0;
    section = temporary;
  }

  /* Nearby edits can expose a section previously classified as buried. */
  int editedNeighbor = chunk->edited;
  for (int d = 0; d < 4; d++) {
    const int neighborZ[4] = {0, 0, -1, 1};
    const int neighborX[4] = {-1, 1, 0, 0};
    const Chunk *const neighbor = find_chunk(world, cx + neighborX[d], cz + neighborZ[d]);
    if (neighbor && neighbor->edited) editedNeighbor = 1;
  }
  if (!section || section->voxelCount == 0 || (!section->meshCandidate && !editedNeighbor)) {
    if (!borrowed) section_mesh_destroy(mesh);
    *mesh = result;
    section_destroy(temporary);
    return 1;
  }

  /* Cache exposed faces and count the vertices needed for each material. */
  const int dx[6] = {-1, 1, 0, 0, 0, 0}, dy[6] = {0, 0, -1, 1, 0, 0},
            dz[6] = {0, 0, 0, 0, -1, 1};
  MeshNeighbors neighbors;
  memset(neighbors.sampled, 0, sizeof(neighbors.sampled));
  uint8_t visible[SECTION_VOLUME] = {0};
  size_t counts[BLOCK_TYPE_COUNT] = {0};
  MESH_BENCH_QUERY_BEGIN();
  for (int d = 0; d < 6; d++) {
    neighbors.sections[d] = world_section(world, cx + dx[d], sy + dy[d], cz + dz[d]);
    neighbors.chunks[d] = find_chunk(world, cx + dx[d], cz + dz[d]);
  }
  for (int x = 0; x < SECTION_SIZE; x++)
    for (int y = 0; y < SECTION_SIZE; y++)
      for (int z = 0; z < SECTION_SIZE; z++) {
        const int index = (x * SECTION_SIZE + y) * SECTION_SIZE + z;
        const BlockType type = section_block(section, index);
        if (type == BLOCK_TYPE_EMPTY) continue;
        if (!block_is_solid(type)) { section_destroy(temporary); return 0; }
        for (int d = 0; d < 6; d++) {
          const int nx = x + dx[d], ny = y + dy[d], nz = z + dz[d];
          const BlockType other = nx >= 0 && nx < SECTION_SIZE &&
              ny >= 0 && ny < SECTION_SIZE && nz >= 0 && nz < SECTION_SIZE ?
              section_block(section, (nx * SECTION_SIZE + ny) * SECTION_SIZE + nz) :
              mesh_boundary_block(world, &neighbors, d, cx, sy, cz, nx, ny, nz);
          if (block_face_visible(type, other)) {
            visible[index] |= (uint8_t)(1u << d);
            counts[type] += 6;
          }
        }
      }
  MESH_BENCH_QUERY_END();

  /* Counterclockwise corners viewed from outside: -X,+X,-Y,+Y,-Z,+Z. */
  static const int corners[6][4][3] = {{{0, 0, 1}, {0, 1, 1}, {0, 1, 0}, {0, 0, 0}},
                                       {{1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}},
                                       {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}},
                                       {{0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}},
                                       {{0, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}},
                                       {{1, 0, 1}, {1, 1, 1}, {0, 1, 1}, {0, 0, 1}}};

  /* Reserve a contiguous vertex range for each material. */
  for (int t = 0; t < BLOCK_TYPE_COUNT; t++) {
    result.firstVertex[t] = result.vertexCount;
    result.typeVertexCount[t] = counts[t];
    result.vertexCount += counts[t];
  }
  if (result.vertexCount > SECTION_MESH_MAX_VERTICES ||
      result.vertexCount > SIZE_MAX / sizeof(*result.vertices) ||
      (borrowed && (result.vertexCount > capacity || (result.vertexCount && !scratch)))) {
    section_destroy(temporary);
    return 0;
  }
  result.vertices = result.vertexCount ? (borrowed ? scratch :
      storage_alloc(STORAGE_MESH, result.vertexCount * sizeof(*result.vertices))) : NULL;
  if (result.vertexCount && !result.vertices) {
    section_destroy(temporary);
    return 0;
  }

  size_t cursors[BLOCK_TYPE_COUNT];
  memcpy(cursors, result.firstVertex, sizeof(cursors));
  /* Reuse the visibility mask while emitting contiguous material ranges. */
  for (int x = 0; x < SECTION_SIZE; x++)
    for (int y = 0; y < SECTION_SIZE; y++)
      for (int z = 0; z < SECTION_SIZE; z++) {
        const int index = (x * SECTION_SIZE + y) * SECTION_SIZE + z;
        if (!visible[index]) continue;
        const BlockType type = section_block(section, index);
        const int wx = cx * SECTION_SIZE + x, wy = sy * SECTION_SIZE + y,
            wz = cz * SECTION_SIZE + z;
        for (int d = 0; d < 6; d++) {
          if (!(visible[index] & (1u << d))) continue;
          static const int order[6] = {0, 1, 2, 0, 2, 3};
          for (int v = 0; v < 6; v++) {
            const int *const corner = corners[d][order[v]];
            result.vertices[cursors[type]++] =
                (SurfaceVertex){.x = world->origin.x + (float)(wx + corner[0]) * world->blockSize,
                                .y = world->origin.y + (float)(wy + corner[1]) * world->blockSize,
                                .z = world->origin.z + (float)(wz + corner[2]) * world->blockSize};
          }
        }
      }

  if (!borrowed) section_mesh_destroy(mesh);
  *mesh = result;
  section_destroy(temporary);
  return 1;
}

int section_mesh_build(const World *const world, const int chunkX, const int sectionY, const int chunkZ,
                       SectionMesh *const mesh) {
  return mesh_build(world, chunkX, sectionY, chunkZ, mesh, NULL, 0, 0);
}
int section_mesh_build_scratch(const World *const world, const int cx, const int sy, const int cz,
                               SurfaceVertex *const scratch, const size_t capacity,
                               SectionMesh *const output) {
  return mesh_build(world, cx, sy, cz, output, scratch, capacity, 1);
}
