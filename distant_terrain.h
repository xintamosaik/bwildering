#ifndef DISTANT_TERRAIN_H
#define DISTANT_TERRAIN_H

#include <GL/glew.h>
#include "world_streaming.h"
#include "performance.h"

/* Fixed capacity for the current 300-unit view at unit block size. Unsupported
   scales/regions fall back to the ordinary voxel renderer. */
enum { DISTANT_REGION_SIDE = 41, DISTANT_CHUNKS = 41 * 41, DISTANT_STEP = 8 };
typedef struct {
  GLuint vao, vbo;
  GLint firstVertex[BLOCK_TYPE_COUNT];
  GLsizei vertexCount[BLOCK_TYPE_COUNT];
  size_t bytes;
  int active;
  StreamRegion region;
  int anchorX, anchorZ, nearChunks;
  int worldMinX, worldMinZ, worldSide;
  uint32_t seed;
  unsigned int relief;
  Vec3 origin;
  float blockSize;
  const World *source; /* Identity only; no borrowed section pointers. */
  unsigned char replaced[DISTANT_CHUNKS];
} DistantTerrain;

/* Single render-thread cache. Failed uploads retain resources but deactivate
   replacement, so full voxel geometry remains the fallback for the frame. */
int distant_terrain_update(DistantTerrain *terrain, const World *world, Vec3 eye, float distance,
                           Performance *performance);
int distant_terrain_replaces(const DistantTerrain *terrain, int chunkX, int chunkZ);
void distant_terrain_draw(const DistantTerrain *terrain, BlockType type, Performance *performance);
void distant_terrain_destroy(DistantTerrain *terrain);

#endif
