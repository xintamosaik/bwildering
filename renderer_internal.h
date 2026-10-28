#ifndef RENDERER_INTERNAL_H
#define RENDERER_INTERNAL_H

#include <GL/glew.h>
#include "renderer.h"
#include "distant_terrain.h"

/* Private renderer layout, embedded by the application; not a drawing API. */
typedef struct {
  int chunkX, sectionY, chunkZ;
  GLuint vao, vbo;
  Vec3 boundsMin, boundsMax;
  GLint firstVertex[BLOCK_TYPE_COUNT];
  GLsizei vertexCount[BLOCK_TYPE_COUNT];
} SectionGpuMesh;

struct Renderer {
  /* Owns GPU resources for one bounded world layout. Destroy while its GL
     context is current, before destroying the window/context. */
  SectionGpuMesh *meshes;
  size_t meshCount, meshCapacity;
  SectionGpuMesh *staging;
  size_t stagingCapacity;
  SurfaceVertex *meshScratch; /* Borrowed through upload; never crosses frames. */
  size_t *visibleMeshes;
  size_t visibleMeshCount, visibleMeshCapacity;
  /* Final camera-visible sections; independent of streaming residency. */
  size_t *drawMeshes;
  size_t drawMeshCount, drawMeshCapacity;
  int streamed;
  Performance *performance;
  double metadataSeconds;
  GLuint textVao;
  GLuint textVbo;
  GLuint program;
  DistantTerrain distant;
  int distantEnabled;
  GLint distantPassLocation;
  GLint mvpLocation;
  GLint colorLocation;
  GLint blockGridLocation;
  GLint waterPassLocation;
  GLint waterViewLocation;
  GLint waterSurfaceLocation;
  float waterSurface;
  Vec3 worldOrigin;
  float blockSize;
  int framebufferWidth;
  int framebufferHeight;
  float viewDistance;
};

#endif
