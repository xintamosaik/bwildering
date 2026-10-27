#ifndef APPLICATION_H
#define APPLICATION_H

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "game_logic.h"
#include "input.h"
#include "performance.h"

/* Internal application state shared with performance instrumentation. */
typedef struct {
  int chunkX, sectionY, chunkZ;
  GLuint vao, vbo;
  Vec3 boundsMin, boundsMax;
  GLint firstVertex[BLOCK_TYPE_COUNT];
  GLsizei vertexCount[BLOCK_TYPE_COUNT];
} SectionGpuMesh;

/* At block size 1 and radius 300 the padded bound is 41*41 columns.
   Sections share this fixed queue; excess work stays dirty and is rescanned
   from a rotating cursor. Smaller blocks and arbitrary edits need no larger queue. */
enum { STREAM_TASK_CAPACITY = 41 * 41 };
typedef struct {
  int x, y, z, materialize;
  uint64_t revision, born;
  double priority;
} StreamTask;
typedef struct {
  double seconds; /* Soft deadline; zero disables the clock limit. */
  size_t operations, bytes; /* Check before each atomic operation. */
} StreamBudget;
typedef enum { STREAM_FAILED = 0, STREAM_COMPLETE = 1, STREAM_DEFERRED = 2 } StreamResult;

typedef struct {
  /* Owns GPU resources for one bounded world layout. Destroy while its GL
     context is current, before destroying the window/context. */
  SectionGpuMesh *meshes;
  size_t meshCount;
  size_t *visibleMeshes;
  size_t visibleMeshCount, visibleMeshCapacity;
  /* Final camera-visible sections; independent of streaming residency. */
  size_t *drawMeshes;
  size_t drawMeshCount, drawMeshCapacity;
  int streamed;
  StreamTask pending[STREAM_TASK_CAPACITY];
  size_t pendingCount, pendingWork, peakPending, scanCursor;
  uint64_t streamFrame;
  Performance *performance;
  double metadataSeconds;
  GLuint textVao;
  GLuint textVbo;
  GLuint program;
  GLint mvpLocation;
  GLint colorLocation;
  int framebufferWidth;
  int framebufferHeight;
  float viewDistance;
} Renderer;

struct Application {
  GLFWwindow *window;
  Player player;
  InputState input;
  Renderer renderer;
  World world;
  Performance performance;
  size_t residentChunks, editedChunks, dirtySections, gpuBytes, residentVoxelBytes;
  double worldMetadataSeconds, startupSeconds;
  double rawFrame;
  double lastFrame;
  double fpsWindowStart;
  double simulationAccumulator;
  int fpsFrameCount;
  int fps;
  int jumpQueued;
  int placeQueued;
  int removeQueued;
  int paused;
  int settingsOpen;
  int glfwInitialized;
  int rendererInitialized;
};

#endif
