#ifndef APPLICATION_INTERNAL_H
#define APPLICATION_INTERNAL_H

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "application.h"
#include "input.h"
#include "game.h"
#include "ui.h"
#include "performance.h"
#include "renderer_internal.h"

/** World-space origin used to center the starter platform. */
static const Vec3 CENTER = {.x = 0.0f, .y = 0.0f, .z = 0.0f};
static const uint32_t WORLD_SEED = UINT32_C(0x4B1D2A37);

struct Application {
  GLFWwindow *window;
  Game game;
  GameplayAim pendingAim; /* Responsive view aim; committed only by game_step. */
  InputState input;
  Renderer renderer;
  WorldStreaming streaming;
  Performance performance;
  size_t residentChunks, editedChunks, dirtySections, gpuBytes, residentVoxelBytes;
  double worldMetadataSeconds, startupSeconds;
  double rawFrame;
  double lastFrame;
  double fpsWindowStart;
  double simulationAccumulator;
  int fpsFrameCount;
  int fps;
  /* One flag per action, retained across zero-tick frames. Repeated presses
     coalesce; the first available tick consumes them, capture changes discard. */
  int jumpQueued;
  int placeQueued;
  int removeQueued;
  int paused;
  UiState ui;
  int glfwInitialized;
  int rendererInitialized;
};

/* Local visual residency policy and shared work allowance. */
StreamResult application_stream_view(WorldStreaming *stream, Renderer *renderer,
                                     World *world, const Player *player,
                                     const StreamBudget *budget);
StreamResult application_stream_step(WorldStreaming *stream, Renderer *renderer,
                                     World *world, const Player *player);

/* Private orchestration boundary for application fixtures and captures. */
int application_init(Application *application);
void application_shutdown(Application *application);
void application_update(Application *application);
void application_render(Application *application);
int application_frame(Application *application);

#endif
