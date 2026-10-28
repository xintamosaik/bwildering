#include "distant_terrain.h"
#include "world_generation.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Four 8x8 cells per column, plus one-block seam segments along its four edges.
   Scratch is shared only during synchronous single-render-thread builds. */
enum { DISTANT_TILES = DISTANT_CHUNKS * 4,
       DISTANT_VERTICES = DISTANT_TILES * 12 + DISTANT_CHUNKS * 4 * SECTION_SIZE * 6 };
typedef struct {
  int x0, z0, x1, z1;
  float heights[4];
  BlockType material;
  unsigned char water, skirts;
} DistantTile;
static DistantTile tiles[DISTANT_TILES];
static SurfaceVertex vertices[DISTANT_VERTICES];

static size_t mask_index(const DistantTerrain *const terrain, const int x, const int z) {
  return (size_t)(x - terrain->region.minX) *
      (size_t)(terrain->region.maxZ - terrain->region.minZ + 1) + (size_t)(z - terrain->region.minZ);
}

int distant_terrain_replaces(const DistantTerrain *const terrain, const int x, const int z) {
  return terrain->active && x >= terrain->region.minX && x <= terrain->region.maxX &&
      z >= terrain->region.minZ && z <= terrain->region.maxZ && terrain->replaced[mask_index(terrain, x, z)];
}

static int same_plan(const DistantTerrain *const a, const DistantTerrain *const b) {
  return a->vao && a->source == b->source && a->seed == b->seed && a->relief == b->relief &&
      a->origin.x == b->origin.x && a->origin.y == b->origin.y && a->origin.z == b->origin.z &&
      a->blockSize == b->blockSize && a->worldMinX == b->worldMinX && a->worldMinZ == b->worldMinZ &&
      a->worldSide == b->worldSide && a->anchorX == b->anchorX && a->anchorZ == b->anchorZ &&
      a->nearChunks == b->nearChunks && a->region.minX == b->region.minX &&
      a->region.maxX == b->region.maxX && a->region.minZ == b->region.minZ &&
      a->region.maxZ == b->region.maxZ && !memcmp(a->replaced, b->replaced, sizeof(a->replaced));
}

static SurfaceVertex vertex(const World *const world, const int x, const float y, const int z) {
  return (SurfaceVertex){world->origin.x + (float)x * world->blockSize,
      world->origin.y + y * world->blockSize, world->origin.z + (float)z * world->blockSize};
}

static void quad(const SurfaceVertex corners[const 4], const BlockType material,
                 size_t cursors[const BLOCK_TYPE_COUNT]) {
  static const int order[6] = {0, 1, 2, 0, 2, 3};
  for (size_t i = 0; i < 6; i++) vertices[cursors[material]++] = corners[order[i]];
}

static float sample_height(const World *const world, const int x, const int z) {
  const int sx = x < world->minX + world->blocksPerSide ? x : x - 1;
  const int sz = z < world->minZ + world->blocksPerSide ? z : z - 1;
  return (float)terrain_sample_world_column(world, sx, sz).stoneTopY;
}

static size_t build_mesh(DistantTerrain *const terrain, const World *const world) {
  size_t tileCount = 0, counts[BLOCK_TYPE_COUNT] = {0};
  for (int cx = terrain->region.minX; cx <= terrain->region.maxX; cx++)
    for (int cz = terrain->region.minZ; cz <= terrain->region.maxZ; cz++) {
      if (!distant_terrain_replaces(terrain, cx, cz)) continue;
      const int startX = cx * SECTION_SIZE > world->minX ? cx * SECTION_SIZE : world->minX;
      const int startZ = cz * SECTION_SIZE > world->minZ ? cz * SECTION_SIZE : world->minZ;
      const int endX = (cx + 1) * SECTION_SIZE < world->minX + world->blocksPerSide ?
          (cx + 1) * SECTION_SIZE : world->minX + world->blocksPerSide;
      const int endZ = (cz + 1) * SECTION_SIZE < world->minZ + world->blocksPerSide ?
          (cz + 1) * SECTION_SIZE : world->minZ + world->blocksPerSide;
      for (int x = startX; x < endX; x += DISTANT_STEP)
        for (int z = startZ; z < endZ; z += DISTANT_STEP) {
          const int x1 = x + DISTANT_STEP < endX ? x + DISTANT_STEP : endX;
          const int z1 = z + DISTANT_STEP < endZ ? z + DISTANT_STEP : endZ;
          const TerrainColumn center = terrain_sample_world_column(world, (x + x1) / 2, (z + z1) / 2);
          DistantTile *const tile = &tiles[tileCount++];
          *tile = (DistantTile){.x0 = x, .z0 = z, .x1 = x1, .z1 = z1,
              .heights = {sample_height(world, x, z), sample_height(world, x, z1),
                          sample_height(world, x1, z1), sample_height(world, x1, z)},
              .material = terrain_column_block(center, center.stoneTopY - 1)};
          counts[tile->material] += 6;
          for (int i = 0; i < 4; i++) tile->water |= world->hasOcean && tile->heights[i] < (float)world->waterTopY;
          if (tile->water) counts[BLOCK_TYPE_WATER] += 6;
          /* Shared coarse vertices meet exactly. Seams bridge the actual voxel
             edge at near/edited boundaries and close the outer ring. */
          if (x == startX && !distant_terrain_replaces(terrain, cx - 1, cz)) tile->skirts |= 1u;
          if (z1 == endZ && !distant_terrain_replaces(terrain, cx, cz + 1)) tile->skirts |= 2u;
          if (x1 == endX && !distant_terrain_replaces(terrain, cx + 1, cz)) tile->skirts |= 4u;
          if (z == startZ && !distant_terrain_replaces(terrain, cx, cz - 1)) tile->skirts |= 8u;
          for (unsigned int i = 0; i < 4u; i++)
            if (tile->skirts & (1u << i)) counts[BLOCK_TYPE_STONE] +=
                (size_t)(i % 2u ? x1 - x : z1 - z) * 6u;
        }
    }
  size_t cursors[BLOCK_TYPE_COUNT], total = 0;
  for (int type = 0; type < BLOCK_TYPE_COUNT; type++) {
    terrain->firstVertex[type] = (GLint)total;
    terrain->vertexCount[type] = (GLsizei)counts[type];
    cursors[type] = total;
    total += counts[type];
  }
  for (size_t t = 0; t < tileCount; t++) {
    const DistantTile *const tile = &tiles[t];
    const int xs[4] = {tile->x0, tile->x0, tile->x1, tile->x1};
    const int zs[4] = {tile->z0, tile->z1, tile->z1, tile->z0};
    SurfaceVertex top[4], water[4];
    for (int i = 0; i < 4; i++) {
      top[i] = vertex(world, xs[i], tile->heights[i], zs[i]);
      water[i] = vertex(world, xs[i], (float)world->waterTopY, zs[i]);
    }
    quad(top, tile->material, cursors);
    if (tile->water) quad(water, BLOCK_TYPE_WATER, cursors);
    for (unsigned int i = 0; i < 4u; i++) if (tile->skirts & (1u << i)) {
      const unsigned int j = (i + 1u) % 4u;
      const int length = i % 2u ? tile->x1 - tile->x0 : tile->z1 - tile->z0;
      const int dx = (xs[j] - xs[i]) / length, dz = (zs[j] - zs[i]) / length;
      for (int step = 0; step < length; step++) {
        const int ax = xs[i] + step * dx, az = zs[i] + step * dz;
        const int bx = ax + dx, bz = az + dz;
        const int sx = (ax < bx ? ax : bx) - (i == 0u);
        const int sz = (az < bz ? az : bz) - (i == 3u);
        const int inWorld = sx >= world->minX && sx < world->minX + world->blocksPerSide &&
            sz >= world->minZ && sz < world->minZ + world->blocksPerSide;
        const float solid = inWorld ? (float)terrain_sample_world_column(world, sx, sz).stoneTopY :
            (float)TERRAIN_BASE_Y;
        const float slope = tile->heights[j] - tile->heights[i];
        const float a = tile->heights[i] + slope * (float)step / (float)length;
        const float b = tile->heights[i] + slope * (float)(step + 1) / (float)length;
        const SurfaceVertex side[4] = {vertex(world, ax, a, az), vertex(world, bx, b, bz),
            vertex(world, bx, solid, bz), vertex(world, ax, solid, az)};
        quad(side, BLOCK_TYPE_STONE, cursors);
      }
    }
  }
  return total;
}

void distant_terrain_destroy(DistantTerrain *const terrain) {
  if (terrain->vao) glDeleteVertexArrays(1, &terrain->vao);
  if (terrain->vbo) glDeleteBuffers(1, &terrain->vbo);
  *terrain = (DistantTerrain){0};
}

int distant_terrain_update(DistantTerrain *const terrain, const World *const world, const Vec3 eye,
                           const float distance, Performance *const performance) {
  terrain->active = 0;
  if (!world->proceduralTerrain || !isfinite(eye.x) || !isfinite(eye.y) || !isfinite(eye.z) ||
      !isfinite(distance) || distance <= 96.0f) return 1;
  const float vx = (eye.x - world->origin.x) / world->blockSize;
  const float vz = (eye.z - world->origin.z) / world->blockSize;
  if (vx < (float)world->minX || vx >= (float)(world->minX + world->blocksPerSide) ||
      vz < (float)world->minZ || vz >= (float)(world->minZ + world->blocksPerSide) ||
      (world->hasOcean && eye.y < world->origin.y + (float)world->waterTopY * world->blockSize) ||
      eye.y <= world_support_height(world, eye.x, eye.z)) return 1;
  DistantTerrain next = {.active = 1, .source = world, .origin = world->origin,
      .blockSize = world->blockSize, .seed = world->terrainSeed, .relief = world->terrainRelief,
      .worldMinX = world->minX, .worldMinZ = world->minZ, .worldSide = world->blocksPerSide,
      .anchorX = voxel_floor_div((int)floorf(vx), SECTION_SIZE),
      .anchorZ = voxel_floor_div((int)floorf(vz), SECTION_SIZE)};
  const float span = world->blockSize * (float)SECTION_SIZE;
  const float nearChunks = ceilf(96.0f / span);
  if (nearChunks > (float)DISTANT_REGION_SIDE ||
      !world_stream_region(world, eye, distance, &next.region) ||
      next.region.maxX - next.region.minX + 1 > DISTANT_REGION_SIDE ||
      next.region.maxZ - next.region.minZ + 1 > DISTANT_REGION_SIDE) return 1;
  next.nearChunks = (int)nearChunks;
  for (int x = next.region.minX; x <= next.region.maxX; x++)
    for (int z = next.region.minZ; z <= next.region.maxZ; z++) {
      const Chunk *const chunk = stream_chunk(world, x, z);
      int edited = chunk->edited;
      static const int dx[4] = {-1, 1, 0, 0}, dz[4] = {0, 0, -1, 1};
      for (int i = 0; i < 4; i++) {
        const int nx = x + dx[i], nz = z + dz[i];
        if (nx < world->minChunkX || nx >= world->minChunkX + world->chunksPerSide ||
            nz < world->minChunkZ || nz >= world->minChunkZ + world->chunksPerSide) continue;
        edited |= stream_chunk(world, nx, nz)->edited;
      }
      /* Keep a full neighbor apron: edits can expose the side of an otherwise
         unedited column, which a top-surface mesh cannot represent. */
      next.replaced[mask_index(&next, x, z)] = !edited &&
          (abs(x - next.anchorX) > next.nearChunks || abs(z - next.anchorZ) > next.nearChunks);
    }
  if (same_plan(terrain, &next)) { terrain->active = 1; return 1; }
  const double start = perf_clock(performance);
  const size_t count = build_mesh(&next, world);
  next.bytes = count * sizeof(*vertices);
  if (performance && performance->enabled) {
    performance->frame.mesh += perf_clock(performance) - start;
    if (next.bytes > performance->frame.peakMeshBytes) performance->frame.peakMeshBytes = next.bytes;
  }
  const double uploadStart = perf_clock(performance);
  glGenVertexArrays(1, &next.vao);
  glGenBuffers(1, &next.vbo);
  if (!next.vao || !next.vbo) { distant_terrain_destroy(&next); return 0; }
  glBindVertexArray(next.vao);
  glBindBuffer(GL_ARRAY_BUFFER, next.vbo);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)next.bytes, vertices, GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SurfaceVertex), (void *)0);
  glEnableVertexAttribArray(0);
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  if (glGetError() != GL_NO_ERROR) { distant_terrain_destroy(&next); return 0; }
  if (performance && performance->enabled) {
    performance->frame.uploadCalls++;
    performance->frame.uploadBytes += next.bytes;
    performance->frame.rebuilds++;
    performance->frame.upload += perf_clock(performance) - uploadStart;
  }
  distant_terrain_destroy(terrain);
  *terrain = next;
  return 1;
}

void distant_terrain_draw(const DistantTerrain *const terrain, const BlockType type, Performance *const performance) {
  if (!terrain->active || !terrain->vertexCount[type]) return;
  glBindVertexArray(terrain->vao);
  glDrawArrays(GL_TRIANGLES, terrain->firstVertex[type], terrain->vertexCount[type]);
  if (performance && performance->enabled) {
    performance->frame.draws++;
    performance->frame.triangles += (size_t)terrain->vertexCount[type] / 3u;
  }
}
