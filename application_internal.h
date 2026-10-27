#ifndef APPLICATION_INTERNAL_H
#define APPLICATION_INTERNAL_H

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "application.h"
#include "input.h"
#include "ui.h"
#include "performance.h"
#include "renderer_internal.h"

/** World-space origin used to center the starter platform. */
static const Vec3 CENTER = {.x = 0.0f, .y = 0.0f, .z = 0.0f};
static const uint32_t WORLD_SEED = UINT32_C(0x4B1D2A37);

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
  UiState ui;
  int glfwInitialized;
  int rendererInitialized;
};

/* Private orchestration boundary for application fixtures and captures. */
int application_init(Application *application);
void application_shutdown(Application *application);
void application_update(Application *application);
void application_render(Application *application);
int application_frame(Application *application);

#endif
