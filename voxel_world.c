#include "game_logic.h"

#include <math.h>
#include <stdlib.h>
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
  free(section->blocks);
  free(section);
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

  free(section->blocks);
  section->blocks = NULL;
  section->material = material;
  section->storage = section->voxelCount ? SECTION_UNIFORM : SECTION_EMPTY;
}

/* Allocate first; callers commit ownership/counts only after success. */
static int section_expand(Subchunk *const section) {
  if (section->storage == SECTION_DENSE) return 1;
  uint8_t *const blocks = malloc(SECTION_VOLUME);
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
  free(world->chunks);
  *world = (World){0};
}

int world_init(World *const world, const int side, const Vec3 center, const float size) {
  Vec3 origin = center;
  origin.x -= (side % 2) * size * 0.5f;
  origin.z -= (side % 2) * size * 0.5f;
  return world_init_region(world, -(side / 2), -(side / 2), side, origin, size);
}

int world_init_region(World *const world, const int minX, const int minZ, const int side, Vec3 origin,
                       const float size) {
  /* Validate in wide arithmetic before endpoint, chunk or neighbour arithmetic. */
  if (!world || side <= 0 || side > WORLD_MAX_BLOCKS_PER_SIDE ||
      minX < WORLD_COORD_MIN || minZ < WORLD_COORD_MIN ||
      (int64_t)minX + side - 1 > WORLD_COORD_MAX ||
      (int64_t)minZ + side - 1 > WORLD_COORD_MAX || !isfinite(size) ||
      size < 0.001f || !isfinite(origin.x) || !isfinite(origin.y) || !isfinite(origin.z))
    return 0;

  /* Keep each block at least one float ULP wide throughout the bounded world. */
  const float origins[3] = {origin.x, origin.y, origin.z};
  const float extents[3] = {
      fmaxf(fabsf((float)minX), fabsf((float)(minX + side))),
      WORLD_SECTION_COUNT * SECTION_SIZE,
      fmaxf(fabsf((float)minZ), fabsf((float)(minZ + side)))};
  for (int axis = 0; axis < 3; axis++) {
    const float limit = fabsf(origins[axis]) + extents[axis] * size;
    if (!isfinite(limit) || nextafterf(limit, INFINITY) - limit > size)
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
  result.blockSize = size;
  result.origin = origin;
  result.chunks = calloc(result.chunkCount, sizeof(*result.chunks));
  if (!result.chunks)
    return 0;
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

static Chunk *find_chunk_mut(World *const world, const int x, const int z) {
  size_t index;
  return find_chunk_index(world, x, z, &index) ? &world->chunks[index] : NULL;
}

static Subchunk **section_slot(Chunk *const chunk, const int y) {
  Subchunk **slot = &chunk->sections;
  while (*slot && (*slot)->sectionY < y) slot = &(*slot)->next;
  return slot;
}

const Subchunk *world_section(const World *const world, const int x, const int y, const int z) {
  MESH_BENCH_LOOKUP_BEGIN();
  const Chunk *const chunk = find_chunk(world, x, z);
  const Subchunk *s = chunk ? chunk->sections : NULL;
  while (s && s->sectionY < y) {
    MESH_BENCH_VISIT();
    s = s->next;
  }
  MESH_BENCH_LOOKUP_END();
  return s && s->sectionY == y ? s : NULL;
}

Subchunk *world_section_mut(World *const world, const int x, const int y, const int z) {
  return (Subchunk *)world_section(world, x, y, z);
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

BlockType world_get_block(const World *const world, const int x, const int y, const int z) {
  if (!in_bounds(world, x, y, z))
    return BLOCK_TYPE_EMPTY;
  const Subchunk *const section = world_section(world, voxel_section_coord(x), voxel_section_coord(y),
                                    voxel_section_coord(z));
  if (section)
    return section_block(section, block_index(x, y, z));

  /* Unmaterialized chunks still answer reads from procedural terrain. */
  const Chunk *const chunk = find_chunk(world, voxel_section_coord(x), voxel_section_coord(z));
  if (world->proceduralTerrain && !chunk->terrainReady &&
      y >= TERRAIN_BASE_Y && y < (WORLD_MIN_SECTION_Y + WORLD_SECTION_COUNT) * SECTION_SIZE)
    return terrain_column_block(MESH_BENCH_SAMPLE(world->terrainSeed, x, z), y);
  return BLOCK_TYPE_EMPTY;
}

static Subchunk *terrain_section(const World *const world, const int cx, const int sy, const int cz,
                                 const TerrainColumn columns[SECTION_SIZE][SECTION_SIZE]) {
  Subchunk *const section = malloc(sizeof(*section));
  if (!section)
    return NULL;
  *section = (Subchunk){.storage = SECTION_EMPTY, .material = BLOCK_TYPE_EMPTY,
      .dirty = 1, .meshCandidate = 1, .sectionY = sy};
  if (sy < WORLD_MIN_SECTION_Y || sy >= WORLD_MIN_SECTION_Y + WORLD_SECTION_COUNT)
    return section;

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
        const BlockType type = terrain_column_block(column, sy * SECTION_SIZE + y);
        blocks[(x * SECTION_SIZE + y) * SECTION_SIZE + z] = (uint8_t)type;
        section->voxelCount += type != BLOCK_TYPE_EMPTY;
      }
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

int world_materialize_chunk(World *const world, const int cx, const int cz) {
  Chunk *const chunk = find_chunk_mut(world, cx, cz);
  if (!chunk || !world->proceduralTerrain)
    return 0;
  if (chunk->terrainReady)
    return 1;

  /* Sample each column once for all vertical sections in this chunk. */
  TerrainColumn columns[SECTION_SIZE][SECTION_SIZE];
  int highest = TERRAIN_BASE_Y;
  for (int x = 0; x < SECTION_SIZE; x++)
    for (int z = 0; z < SECTION_SIZE; z++) {
      const int wx = cx * SECTION_SIZE + x, wz = cz * SECTION_SIZE + z;
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
      const int wx = cx * SECTION_SIZE + x, wz = cz * SECTION_SIZE + z;
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
    if (world_section(world, cx, sy, cz) || sy * SECTION_SIZE >= highest ||
        (sy + 1) * SECTION_SIZE <= TERRAIN_BASE_Y)
      continue;
    pending[s] = terrain_section(world, cx, sy, cz, columns);
    if (pending[s]) pending[s]->meshCandidate = edge || sy == WORLD_MIN_SECTION_Y ||
        (sy + 1) * SECTION_SIZE >= lowest;
    if (!pending[s]) {
      for (int j = 0; j < WORLD_SECTION_COUNT; j++) section_destroy(pending[j]);
      return 0;
    }
  }

  /* Commit the staged sections and their counts together. */
  for (int s = 0; s < WORLD_SECTION_COUNT; s++)
    if (pending[s]) {
      Subchunk **const slot = section_slot(chunk, pending[s]->sectionY);
      pending[s]->next = *slot;
      *slot = pending[s];
      world->sectionCount++;
      world->voxelCount += pending[s]->voxelCount;
    }

  chunk->terrainReady = 1;
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
  return 1;
}

void world_release_chunk(World *const world, const int cx, const int cz) {
  Chunk *const chunk = find_chunk_mut(world, cx, cz);
  if (!chunk || !world->proceduralTerrain || chunk->edited)
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
      Chunk *const chunk = find_chunk_mut(world, x, z);
      if (chunk && !chunk->edited && (chunk->terrainReady || chunk->sections)) {
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

  /* Prepare writable storage before linking a new section into the world. */
  Subchunk *const section = missing ?
      (world->proceduralTerrain && !chunk->terrainReady ?
       terrain_section(world, cx, sy, cz, NULL) : calloc(1, sizeof(*section))) : *slot;
  if (!section) return 0;
  if (missing && section->storage == SECTION_EMPTY) section->material = BLOCK_TYPE_EMPTY;
  const BlockType old = section_block(section, index);
  if (!section_expand(section)) {
    if (missing) section_destroy(section);
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
  if (world->proceduralTerrain)
    chunk->edited = 1;

  /* Only face neighbors depend on the changed voxel. Keep empty sections as
     tombstones so the renderer can retire their previous GPU meshes. */
  const int dx[6] = {-1, 1, 0, 0, 0, 0}, dy[6] = {0, 0, -1, 1, 0, 0},
            dz[6] = {0, 0, 0, 0, -1, 1};
  int local[3] = {voxel_local_coord(x), voxel_local_coord(y), voxel_local_coord(z)};
  for (int d = 0; d < 6; d++) {
    if ((d == 0 && local[0] != 0) || (d == 1 && local[0] != 15) ||
        (d == 2 && local[1] != 0) || (d == 3 && local[1] != 15) ||
        (d == 4 && local[2] != 0) || (d == 5 && local[2] != 15))
      continue;
    Subchunk *const neighbor = world_section_mut(world, cx + dx[d], sy + dy[d], cz + dz[d]);
    if (neighbor) {
      neighbor->dirty = 1;
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
  if (vx < world->minX || vx > world->minX + world->blocksPerSide || vz < world->minZ ||
      vz > world->minZ + world->blocksPerSide)
    return NAN;
  const int ix = vx == world->minX + world->blocksPerSide
               ? world->minX + world->blocksPerSide - 1
               : (int)floorf(vx);
  const int iz = vz == world->minZ + world->blocksPerSide
               ? world->minZ + world->blocksPerSide - 1
               : (int)floorf(vz);
  const Chunk *const chunk = find_chunk(world, voxel_section_coord(ix), voxel_section_coord(iz));
  int top = WORLD_COORD_MIN;
  int found = 0;

  /* Start with the generated surface, accounting for explicit removals. */
  if (world->proceduralTerrain && !chunk->terrainReady) {
    TerrainColumn column = terrain_sample_column(world->terrainSeed, ix, iz);
    int y = column.height - 1;
    while (y >= TERRAIN_BASE_Y) {
      const Subchunk *const section = world_section(world, voxel_section_coord(ix), voxel_section_coord(y), voxel_section_coord(iz));
      if (!section || block_is_solid(section_block(section, block_index(ix, y, iz)))) break;
      y--;
    }
    if (y >= TERRAIN_BASE_Y) { top = y + 1; found = 1; }
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
  return found ? world->origin.y + top * world->blockSize : NAN;
}

void section_mesh_destroy(SectionMesh *const mesh) {
  if (!mesh)
    return;
  free(mesh->vertices);
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
  if (!world->proceduralTerrain || !cache->chunks[d] || cache->chunks[d]->terrainReady ||
      wy < TERRAIN_BASE_Y || wy >= (WORLD_MIN_SECTION_Y + WORLD_SECTION_COUNT) * SECTION_SIZE)
    return BLOCK_TYPE_EMPTY;
  if (!cache->sampled[x + 1][z + 1]) {
    cache->columns[x + 1][z + 1] = MESH_BENCH_SAMPLE(world->terrainSeed, wx, wz);
    cache->sampled[x + 1][z + 1] = 1;
  }
  return terrain_column_block(cache->columns[x + 1][z + 1], wy);
}

int section_mesh_build(const World *const world, const int cx, const int sy, const int cz, SectionMesh *const mesh) {
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
  const int neighborX[4] = {-1, 1, 0, 0}, neighborZ[4] = {0, 0, -1, 1};
  for (int d = 0; d < 4; d++) {
    const Chunk *const neighbor = find_chunk(world, cx + neighborX[d], cz + neighborZ[d]);
    if (neighbor && neighbor->edited) editedNeighbor = 1;
  }
  if (!section || section->voxelCount == 0 || (!section->meshCandidate && !editedNeighbor)) {
    section_mesh_destroy(mesh);
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
  /* A section has at most 4096*6*6 vertices. */
  result.vertices = result.vertexCount
                        ? malloc(result.vertexCount * sizeof(*result.vertices)) : NULL;
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
                (SurfaceVertex){world->origin.x + (wx + corner[0]) * world->blockSize,
                                world->origin.y + (wy + corner[1]) * world->blockSize,
                                world->origin.z + (wz + corner[2]) * world->blockSize};
          }
        }
      }

  section_mesh_destroy(mesh);
  *mesh = result;
  section_destroy(temporary);
  return 1;
}
