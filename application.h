#ifndef APPLICATION_H
#define APPLICATION_H

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "game_logic.h"
#include "performance.h"

/* Internal application state shared with performance instrumentation. */
typedef struct {
  double lastX;
  double lastY;
  double deltaX;
  double deltaY;
  int firstMouse;
} MouseInput;

typedef struct {
  int chunkX, sectionY, chunkZ;
  GLuint vao, vbo;
  Vec3 boundsMin, boundsMax;
  GLint firstVertex[BLOCK_TYPE_COUNT];
  GLsizei vertexCount[BLOCK_TYPE_COUNT];
} SectionGpuMesh;

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
  MouseInput mouseInput;
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
  int jumpWasDown;
  int jumpQueued;
  int placeQueued;
  int removeQueued;
  int paused;
  int settingsOpen;
  int escapeWasDown;
  int glfwInitialized;
  int rendererInitialized;
};

#endif
