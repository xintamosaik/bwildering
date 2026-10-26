#include "game_logic.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

int voxel_floor_div(int coordinate, int divisor) {
  return coordinate / divisor - (coordinate % divisor < 0);
}

int voxel_floor_mod(int coordinate, int divisor) {
  int remainder = coordinate % divisor;
  return remainder < 0 ? remainder + divisor : remainder;
}

int voxel_section_coord(int coordinate) {
  return voxel_floor_div(coordinate, SECTION_SIZE);
}

int voxel_local_coord(int coordinate) {
  return voxel_floor_mod(coordinate, SECTION_SIZE);
}

int block_is_solid(BlockType type) {
  return type >= BLOCK_TYPE_GRASS && type < BLOCK_TYPE_COUNT;
}

int block_face_visible(BlockType type, BlockType neighbor) {
  /* All current materials are opaque, including the water placeholder. */
  return block_is_solid(type) && !block_is_solid(neighbor);
}

void world_destroy(World *world) {
  if (!world)
    return;
  for (size_t c = 0; c < world->chunkCount; c++)
    for (int s = 0; s < WORLD_SECTION_COUNT; s++)
      free(world->chunks[c].sections[s]);
  free(world->chunks);
  *world = (World){0};
}

int world_init(World *world, int side, Vec3 center, float size) {
  Vec3 origin = center;
  origin.x -= (side % 2) * size * 0.5f;
  origin.z -= (side % 2) * size * 0.5f;
  return world_init_region(world, -(side / 2), -(side / 2), side, origin, size);
}

int world_init_region(World *world, int minX, int minZ, int side, Vec3 origin,
                       float size) {
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
    float limit = fabsf(origins[axis]) + extents[axis] * size;
    if (!isfinite(limit) || nextafterf(limit, INFINITY) - limit > size)
      return 0;
  }
  World result = {0};
  result.minX = minX;
  result.minZ = minZ;
  result.minChunkX = voxel_section_coord(minX);
  result.minChunkZ = voxel_section_coord(minZ);
  int chunksX = voxel_section_coord(minX + side - 1) - result.minChunkX + 1;
  int chunksZ = voxel_section_coord(minZ + side - 1) - result.minChunkZ + 1;
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
      Chunk *chunk = &result.chunks[x * result.chunksPerSide + z];
      chunk->x = result.minChunkX + x;
      chunk->z = result.minChunkZ + z;
    }
  world_destroy(world);
  *world = result;
  return 1;
}

static Chunk *find_chunk(const World *world, int x, int z) {
  if (!world || !world->chunks || x < world->minChunkX || z < world->minChunkZ ||
      x >= world->minChunkX + world->chunksPerSide ||
      z >= world->minChunkZ + world->chunksPerSide)
    return NULL;
  return &world->chunks[(x - world->minChunkX) * world->chunksPerSide + z -
                        world->minChunkZ];
}

Subchunk *world_section(const World *world, int x, int y, int z) {
  Chunk *chunk = find_chunk(world, x, z);
  if (!chunk || y < WORLD_MIN_SECTION_Y || y >= WORLD_MIN_SECTION_Y + WORLD_SECTION_COUNT)
    return NULL;
  return chunk->sections[y - WORLD_MIN_SECTION_Y];
}

static int in_bounds(const World *world, int x, int y, int z) {
  return world && world->chunks && x >= world->minX && z >= world->minZ &&
         x < world->minX + world->blocksPerSide &&
         z < world->minZ + world->blocksPerSide &&
         y >= WORLD_MIN_SECTION_Y * SECTION_SIZE &&
         y < (WORLD_MIN_SECTION_Y + WORLD_SECTION_COUNT) * SECTION_SIZE;
}

static int block_index(int x, int y, int z) {
  return (voxel_local_coord(x) * SECTION_SIZE + voxel_local_coord(y)) * SECTION_SIZE +
         voxel_local_coord(z);
}

BlockType world_get_block(const World *world, int x, int y, int z) {
  if (!in_bounds(world, x, y, z))
    return BLOCK_TYPE_EMPTY;
  Subchunk *section = world_section(world, voxel_section_coord(x), voxel_section_coord(y),
                                    voxel_section_coord(z));
  if (section)
    return (BlockType)section->blocks[block_index(x, y, z)];
  Chunk *chunk = find_chunk(world, voxel_section_coord(x), voxel_section_coord(z));
  if (world->proceduralTerrain && !chunk->terrainReady)
    return terrain_column_block(terrain_sample_column(world->terrainSeed, x, z), y);
  return BLOCK_TYPE_EMPTY;
}

static Subchunk *terrain_section(const World *world, int cx, int sy, int cz,
                                 const TerrainColumn columns[SECTION_SIZE][SECTION_SIZE]) {
  Subchunk *section = malloc(sizeof(*section));
  if (!section)
    return NULL;
  memset(section->blocks, BLOCK_TYPE_EMPTY, sizeof(section->blocks));
  section->voxelCount = 0;
  section->dirty = 1;
  for (int x = 0; x < SECTION_SIZE; x++)
    for (int z = 0; z < SECTION_SIZE; z++) {
      int wx = cx * SECTION_SIZE + x, wz = cz * SECTION_SIZE + z;
      if (wx < world->minX || wx >= world->minX + world->blocksPerSide ||
          wz < world->minZ || wz >= world->minZ + world->blocksPerSide)
        continue;
      TerrainColumn column = columns ? columns[x][z] :
          terrain_sample_column(world->terrainSeed, wx, wz);
      for (int y = 0; y < SECTION_SIZE; y++) {
        BlockType type = terrain_column_block(column, sy * SECTION_SIZE + y);
        section->blocks[(x * SECTION_SIZE + y) * SECTION_SIZE + z] = (uint8_t)type;
        section->voxelCount += type != BLOCK_TYPE_EMPTY;
      }
    }
  return section;
}

int world_materialize_chunk(World *world, int cx, int cz) {
  Chunk *chunk = find_chunk(world, cx, cz);
  if (!chunk || !world->proceduralTerrain)
    return 0;
  if (chunk->terrainReady)
    return 1;
  TerrainColumn columns[SECTION_SIZE][SECTION_SIZE];
  int highest = TERRAIN_BASE_Y;
  for (int x = 0; x < SECTION_SIZE; x++)
    for (int z = 0; z < SECTION_SIZE; z++) {
      int wx = cx * SECTION_SIZE + x, wz = cz * SECTION_SIZE + z;
      columns[x][z] = (TerrainColumn){TERRAIN_BASE_Y, BLOCK_TYPE_STONE};
      if (wx >= world->minX && wx < world->minX + world->blocksPerSide &&
          wz >= world->minZ && wz < world->minZ + world->blocksPerSide) {
        columns[x][z] = terrain_sample_column(world->terrainSeed, wx, wz);
        if (columns[x][z].height > highest)
          highest = columns[x][z].height;
      }
    }
  Subchunk *pending[WORLD_SECTION_COUNT] = {0};
  for (int s = 0; s < WORLD_SECTION_COUNT; s++) {
    int sy = WORLD_MIN_SECTION_Y + s;
    if (chunk->sections[s] || sy * SECTION_SIZE >= highest ||
        (sy + 1) * SECTION_SIZE <= TERRAIN_BASE_Y)
      continue;
    pending[s] = terrain_section(world, cx, sy, cz, columns);
    if (!pending[s]) {
      for (int j = 0; j < WORLD_SECTION_COUNT; j++) free(pending[j]);
      return 0;
    }
  }
  for (int s = 0; s < WORLD_SECTION_COUNT; s++)
    if (pending[s]) {
      chunk->sections[s] = pending[s];
      world->sectionCount++;
      world->voxelCount += pending[s]->voxelCount;
    }
  chunk->terrainReady = 1;
  return 1;
}

void world_release_chunk(World *world, int cx, int cz) {
  Chunk *chunk = find_chunk(world, cx, cz);
  if (!chunk || !world->proceduralTerrain || chunk->edited)
    return;
  for (int s = 0; s < WORLD_SECTION_COUNT; s++)
    if (chunk->sections[s]) {
      world->voxelCount -= chunk->sections[s]->voxelCount;
      world->sectionCount--;
      free(chunk->sections[s]);
      chunk->sections[s] = NULL;
    }
  chunk->terrainReady = 0;
}

int world_set_block(World *world, int x, int y, int z, BlockType type) {
  if (!in_bounds(world, x, y, z) || (!block_is_solid(type) && type != BLOCK_TYPE_EMPTY))
    return 0;
  int cx = voxel_section_coord(x), sy = voxel_section_coord(y),
      cz = voxel_section_coord(z);
  Chunk *chunk = find_chunk(world, cx, cz);
  Subchunk **slot = &chunk->sections[sy - WORLD_MIN_SECTION_Y];
  if (!*slot) {
    if (world_get_block(world, x, y, z) == type)
      return 1;
    Subchunk *section = world->proceduralTerrain && !chunk->terrainReady ?
        terrain_section(world, cx, sy, cz, NULL) : malloc(sizeof(*section));
    if (!section)
      return 0;
    if (!world->proceduralTerrain || chunk->terrainReady) {
      memset(section->blocks, BLOCK_TYPE_EMPTY, sizeof(section->blocks));
      section->voxelCount = 0;
      section->dirty = 0;
    }
    *slot = section;
    world->sectionCount++;
    world->voxelCount += section->voxelCount;
  }
  Subchunk *section = *slot;
  int index = block_index(x, y, z);
  BlockType old = (BlockType)section->blocks[index];
  if (old == type)
    return 1;
  if (old == BLOCK_TYPE_EMPTY) {
    section->voxelCount++;
    world->voxelCount++;
  }
  if (type == BLOCK_TYPE_EMPTY) {
    section->voxelCount--;
    world->voxelCount--;
  }
  section->blocks[index] = (uint8_t)type;
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
    Subchunk *neighbor = world_section(world, cx + dx[d], sy + dy[d], cz + dz[d]);
    if (neighbor)
      neighbor->dirty = 1;
  }
  return 1;
}

float world_surface_height(const World *world, float x, float z) {
  if (!world || !world->chunks || !isfinite(x) || !isfinite(z))
    return NAN;
  float vx = (x - world->origin.x) / world->blockSize;
  float vz = (z - world->origin.z) / world->blockSize;
  if (vx < world->minX || vx > world->minX + world->blocksPerSide || vz < world->minZ ||
      vz > world->minZ + world->blocksPerSide)
    return NAN;
  int ix = vx == world->minX + world->blocksPerSide
               ? world->minX + world->blocksPerSide - 1
               : (int)floorf(vx);
  int iz = vz == world->minZ + world->blocksPerSide
               ? world->minZ + world->blocksPerSide - 1
               : (int)floorf(vz);
  for (int y = (WORLD_MIN_SECTION_Y + WORLD_SECTION_COUNT) * SECTION_SIZE - 1;
       y >= WORLD_MIN_SECTION_Y * SECTION_SIZE; y--)
    if (block_is_solid(world_get_block(world, ix, y, iz)))
      return world->origin.y + (y + 1) * world->blockSize;
  return NAN;
}

void section_mesh_destroy(SectionMesh *mesh) {
  if (!mesh)
    return;
  free(mesh->vertices);
  *mesh = (SectionMesh){0};
}

int section_mesh_build(const World *world, int cx, int sy, int cz, SectionMesh *mesh) {
  Chunk *chunk = find_chunk(world, cx, cz);
  if (!mesh || !chunk || sy < WORLD_MIN_SECTION_Y ||
      sy >= WORLD_MIN_SECTION_Y + WORLD_SECTION_COUNT)
    return 0;
  SectionMesh result = {0};
  Subchunk *section = world_section(world, cx, sy, cz);
  Subchunk *temporary = NULL;
  if (!section && world->proceduralTerrain && !chunk->terrainReady) {
    temporary = terrain_section(world, cx, sy, cz, NULL);
    if (!temporary)
      return 0;
    section = temporary;
  }
  if (!section || section->voxelCount == 0) {
    section_mesh_destroy(mesh);
    *mesh = result;
    free(temporary);
    return 1;
  }
  const int dx[6] = {-1, 1, 0, 0, 0, 0}, dy[6] = {0, 0, -1, 1, 0, 0},
            dz[6] = {0, 0, 0, 0, -1, 1};
  /* Counterclockwise corners viewed from outside: -X,+X,-Y,+Y,-Z,+Z. */
  static const int corners[6][4][3] = {{{0, 0, 1}, {0, 1, 1}, {0, 1, 0}, {0, 0, 0}},
                                       {{1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}},
                                       {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}},
                                       {{0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}},
                                       {{0, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}},
                                       {{1, 0, 1}, {1, 1, 1}, {0, 1, 1}, {0, 0, 1}}};
  size_t counts[BLOCK_TYPE_COUNT] = {0};
  /* Two passes retain contiguous per-material draw ranges. */
  for (int pass = 0; pass < 2; pass++) {
    size_t cursors[BLOCK_TYPE_COUNT];
    for (int t = 0; t < BLOCK_TYPE_COUNT; t++)
      cursors[t] = result.firstVertex[t];
    for (int x = 0; x < 16; x++)
      for (int y = 0; y < 16; y++)
        for (int z = 0; z < 16; z++) {
          BlockType type = (BlockType)section->blocks[(x * 16 + y) * 16 + z];
          if (type == BLOCK_TYPE_EMPTY)
            continue;
          if (!block_is_solid(type)) {
            section_mesh_destroy(&result);
            free(temporary);
            return 0;
          }
          int wx = cx * 16 + x, wy = sy * 16 + y, wz = cz * 16 + z;
          for (int d = 0; d < 6; d++) {
            if (!block_face_visible(
                    type, world_get_block(world, wx + dx[d], wy + dy[d], wz + dz[d])))
              continue;
            if (pass == 0) {
              counts[type] += 6;
              continue;
            }
            static const int order[6] = {0, 1, 2, 0, 2, 3};
            for (int v = 0; v < 6; v++) {
              const int *corner = corners[d][order[v]];
              result.vertices[cursors[type]++] =
                  (SurfaceVertex){world->origin.x + (wx + corner[0]) * world->blockSize,
                                  world->origin.y + (wy + corner[1]) * world->blockSize,
                                  world->origin.z + (wz + corner[2]) * world->blockSize};
            }
          }
        }
    if (pass == 0) {
      for (int t = 0; t < BLOCK_TYPE_COUNT; t++) {
        result.firstVertex[t] = result.vertexCount;
        result.typeVertexCount[t] = counts[t];
        result.vertexCount += counts[t];
      }
      /* A section has at most 4096*6*6 vertices. */
      result.vertices = result.vertexCount
                            ? malloc(result.vertexCount * sizeof(*result.vertices))
                            : NULL;
      if (result.vertexCount && !result.vertices) {
        free(temporary);
        return 0;
      }
    }
  }
  section_mesh_destroy(mesh);
  *mesh = result;
  free(temporary);
  return 1;
}
