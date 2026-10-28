#include "storage.h"
#include "world_generation.h"
#include "application_internal.h"
#include "ui.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#pragma region Constants
/** Width, height, and depth of each default platform block, in world units. */
static const float BLOCK_SIZE = 1.0f;
static const double FIXED_DELTA_TIME = 1.0 / 60.0;
static const double MAX_FRAME_DELTA_TIME = 0.25;

#pragma endregion

#pragma region Camera
static void update_look(GameplayAim *const aim, MouseInput *const input) {
  *aim = game_aim(aim->yaw + (float)input->deltaX * 0.1f,
                  aim->pitch + (float)input->deltaY * 0.1f);
  input->deltaX = 0.0;
  input->deltaY = 0.0;
}

#pragma endregion

#pragma region Application
void application_shutdown(Application *const application) {
  if (application->rendererInitialized) {
    renderer_destroy(&application->renderer);
    application->rendererInitialized = 0;
  }
  application->streaming = (WorldStreaming){0};
  game_destroy(&application->game);
  if (application->window != NULL) {
    glfwDestroyWindow(application->window);
    application->window = NULL;
  }
  if (application->glfwInitialized) {
    glfwTerminate();
    application->glfwInitialized = 0;
  }
}

int application_init(Application *const application) {
  *application = (Application){0};

  input_init(&application->input);

  int worldSide = PLATFORM_BLOCKS_PER_SIDE;
  unsigned int relief = TERRAIN_RELIEF_ALL;
  const char *const configuredRelief = getenv("BW_RELIEF");
  if (configuredRelief) {
    if (strcmp(configuredRelief, "flat") == 0) relief = 0u;
    else if (strcmp(configuredRelief, "hills") == 0) relief = TERRAIN_RELIEF_HILLS;
    else if (strcmp(configuredRelief, "mountain") == 0) relief = TERRAIN_RELIEF_MOUNTAINS;
    else if (strcmp(configuredRelief, "both") != 0) {
      fprintf(stderr, "BW_RELIEF must be flat, hills, mountain, or both\n");
      return 0;
    }
  }
  const char *const configuredSide = getenv("BW_WORLD_SIZE");
  if (configuredSide) {
    char *end;
    const long parsed = strtol(configuredSide, &end, 10);
    if (end == configuredSide || *end || parsed < 1 || parsed > WORLD_MAX_BLOCKS_PER_SIDE) {
      fprintf(stderr, "BW_WORLD_SIZE must be an integer from 1 to %d\n",
              WORLD_MAX_BLOCKS_PER_SIDE);
      return 0;
    }
    worldSide = (int)parsed;
  }

  perf_configure(&application->performance);

  if (!glfwInit()) {
    fprintf(stderr, "Failed to initialize GLFW\n");
    return 0;
  }
  application->glfwInitialized = 1;

  if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND) {
    fprintf(stderr,
            "GLFW selected Wayland; retrying with X11 for GLEW compatibility\n");
    glfwTerminate();
    application->glfwInitialized = 0;
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
    if (!glfwInit()) {
      fprintf(stderr, "Failed to initialize GLFW with the X11 platform\n");
      return 0;
    }
    application->glfwInitialized = 1;
  }

  const double startupStart = perf_clock(&application->performance);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_SAMPLES, 4);

  application->window =
      glfwCreateWindow(1920, 1200, "bwildering 0.1", NULL, NULL);
  if (application->window == NULL) {
    fprintf(stderr, "Failed to create GLFW window\n");
    application_shutdown(application);
    return 0;
  }
  glfwMakeContextCurrent(application->window);
  // Keep VSync off while measuring the uncapped development baseline.
  glfwSwapInterval(0);
  // For synchronized gameplay, switch the two swap-interval lines:
  // glfwSwapInterval(1);

  input_attach(&application->input, application->window);
  glfwSetInputMode(application->window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

  glewExperimental = GL_TRUE;
  const GLenum glewStatus = glewInit();
  if (glewStatus != GLEW_OK) {
    fprintf(stderr, "Failed to initialize GLEW: %s\n",
            (const char *)glewGetErrorString(glewStatus));
    application_shutdown(application);
    return 0;
  }

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_MULTISAMPLE);

  if (!renderer_init(&application->renderer)) {
    application_shutdown(application);
    return 0;
  }
  application->rendererInitialized = 1;
  application->renderer.performance = &application->performance;

  glfwGetFramebufferSize(application->window,
                          &application->renderer.framebufferWidth,
                          &application->renderer.framebufferHeight);
  glViewport(0, 0, application->renderer.framebufferWidth,
             application->renderer.framebufferHeight);

  const double worldStart = perf_clock(&application->performance);
  const GameConfig config = {worldSide, CENTER, BLOCK_SIZE, WORLD_SEED};
  if (!game_init_with_relief(&application->game, config, relief) ||
      !world_stream_storage_limits(game_streaming_world(&application->game), MAX_VIEW_DISTANCE)) {
    fprintf(stderr, "Failed to create voxel terrain\n");
    application_shutdown(application);
    return 0;
  }
  application->worldMetadataSeconds = perf_clock(&application->performance) - worldStart;

  application->pendingAim = game_player(&application->game)->aim;

  const double syncStart = perf_clock(&application->performance);
  if (!application_stream_view(&application->streaming, &application->renderer, game_streaming_world(&application->game), game_player(&application->game), NULL)) {
    fprintf(stderr,"Failed to build or upload section meshes\n");
    application_shutdown(application);
    return 0;
  }

  application->performance.frame.sync = perf_clock(&application->performance) - syncStart;
  application->startupSeconds = perf_clock(&application->performance) - startupStart;
  if (application->performance.enabled) {
    fprintf(stdout, "PERF build=%s %s compiler=%s GL_vendor=%s GL_renderer=%s GL_version=%s\n",
            __DATE__, __TIME__, __VERSION__, (const char *)glGetString(GL_VENDOR),
            (const char *)glGetString(GL_RENDERER), (const char *)glGetString(GL_VERSION));
    application->performance.frame.raw = application->startupSeconds;
    perf_record(&application->performance);
    performance_report(application, "startup");
    performance_reset(&application->performance);
  }
  application->lastFrame = glfwGetTime();
  application->fpsWindowStart = application->lastFrame;
  return 1;
}

/* Local-session policy: menus stop simulation. A future remote session would
   continue ticking while its menu is open. Capture transitions retain held
   actions but discard transients, pending aim and accumulated simulation time. */
static void application_discard_gameplay(Application *const application) {
  application->simulationAccumulator = 0.0;
  application->jumpQueued = 0;
  application->placeQueued = 0;
  application->removeQueued = 0;
  application->pendingAim = game_player(&application->game)->aim;
  input_discard(&application->input);
}

static void application_set_paused(Application *const application, const int paused) {
  application->paused = paused;
  application->ui.settingsOpen = 0;
  application_discard_gameplay(application);
  glfwSetInputMode(application->window, GLFW_CURSOR,
                   paused ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
}

static UiPointer application_pointer(const Application *const application) {
  UiPointer pointer = {0};
  glfwGetCursorPos(application->window, &pointer.x, &pointer.y);
  glfwGetWindowSize(application->window, &pointer.windowWidth, &pointer.windowHeight);
  glfwGetFramebufferSize(application->window, &pointer.framebufferWidth, &pointer.framebufferHeight);
  return pointer;
}

static void application_apply_ui(Application *const application, const UiAction action) {
  switch (action.type) {
  case UI_RESUME: application_set_paused(application, 0); break;
  case UI_QUIT: glfwSetWindowShouldClose(application->window, GLFW_TRUE); break;
  case UI_VIEW_DISTANCE:
    renderer_set_view_distance(&application->renderer, action.viewDistance);
    break;
  case UI_NONE: break;
  }
}

/* Once after polling: pause controls take priority over clicks. Menu clicks are
   applied in capture order, immediately after hit testing, until resume/quit.
   Every frame beginning or ending paused is barred from gameplay. */
static int application_route_input(Application *const application) {
  InputState *const input = &application->input;
  if (input->pressed & INPUT_PAUSE) {
    if (application->paused)
      application_apply_ui(application, ui_handle_pause(&application->ui));
    else
      application_set_paused(application, 1);
    application_discard_gameplay(application);
    return 1;
  }
  if (application->paused) {
    UiPointer pointer = application_pointer(application);
    for (int i = 0; i < input->clickCount; i++) {
      pointer.x = input->clicks[i].x;
      pointer.y = input->clicks[i].y;
      const UiAction action = ui_click(&application->ui,
          renderer_view_distance(&application->renderer), pointer);
      application_apply_ui(application, action);
      if (action.type == UI_RESUME || action.type == UI_QUIT)
        break;
    }
    application_discard_gameplay(application);
    return 1;
  }
  input->clickCount = 0;
  return 0;
}

void application_update(Application *const application) {
  GLFWwindow *const window = application->window;
  glfwPollEvents();
  input_poll(&application->input, window);

  const double currentFrame = glfwGetTime();
  double deltaTime = currentFrame - application->lastFrame;
  application->rawFrame = fmax(0, deltaTime);
  application->lastFrame = currentFrame;
  if (deltaTime < 0.0)
    deltaTime = 0.0;
  if (deltaTime > MAX_FRAME_DELTA_TIME)
    deltaTime = MAX_FRAME_DELTA_TIME;

  application->fpsFrameCount++;
  const double fpsElapsed = currentFrame - application->fpsWindowStart;
  if (fpsElapsed >= 1.0) {
    application->fps = (int)(application->fpsFrameCount / fpsElapsed + 0.5);
    application->fpsFrameCount = 0;
    application->fpsWindowStart = currentFrame;
  }

  if (application_route_input(application))
    return;

  /* Live sampling uses the latest held state and aim for every tick this frame,
     not a timestamped reconstruction. Pending jump/place/remove are three
     bounded flags: repeated presses coalesce until the next tick consumes them. */
  update_look(&application->pendingAim, &application->input.mouse);
  const PlayerInput held = input_player(&application->input);
  if (application->input.pressed & INPUT_JUMP)
    application->jumpQueued = 1;
  application->placeQueued |= (application->input.pressed & INPUT_PLACE) != 0;
  application->removeQueued |= (application->input.pressed & INPUT_REMOVE) != 0;

  const int captured = glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED;
  /* No tick means no consumption. Only the first catch-up tick gets presses;
     every tick gets the same sampled held movement/run/crouch and latest aim. */
  application->simulationAccumulator += deltaTime;
  while (application->simulationAccumulator >= FIXED_DELTA_TIME) {
    PlayerInput movement = held;
    movement.jump = application->jumpQueued;
    const GameCommand command = {
        .movement = movement, .yaw = application->pendingAim.yaw,
        .pitch = application->pendingAim.pitch,
        .place = captured && application->placeQueued,
        .remove = captured && application->removeQueued,
        .material = BLOCK_TYPE_STONE,
    };
    game_step(&application->game, command);
    application->jumpQueued = 0;
    application->placeQueued = 0;
    application->removeQueued = 0;
    application->simulationAccumulator -= FIXED_DELTA_TIME;
  }
}

void application_render(Application *const application) {
  Performance *const p = &application->performance;
  double start = perf_clock(p);
  /* Resize callbacks capture dimensions without touching renderer state. Avoid
     window-system queries and redundant viewport calls on ordinary frames. */
  if (application->input.framebufferResized) {
    renderer_set_viewport(&application->renderer, application->input.framebufferWidth,
                          application->input.framebufferHeight);
    application->input.framebufferResized = 0;
  }
  glClearColor(0.15f, 0.15f, 0.2f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  render_world(&application->renderer, player_view(game_player(&application->game),
                                                      application->pendingAim));
  const UiDrawContext draw = renderer_ui_context(&application->renderer);
  const UiFrame frame = {
      .fps = application->fps, .paused = application->paused,
      .playerPosition = game_player(&application->game)->position,
      .viewDistance = renderer_view_distance(&application->renderer),
      .performance = p, .residentChunks = application->residentChunks,
      .sections = game_world(&application->game)->sectionCount, .editedChunks = application->editedChunks,
      .dirtySections = application->dirtySections,
      .neighborhoodSlots = renderer_neighborhood_slots(&application->renderer),
      .gpuBytes = application->gpuBytes,
      .pointer = application->paused ? application_pointer(application) : (UiPointer){0},
  };
  ui_render(&application->ui, &frame, &draw);
  if (p->enabled) p->frame.render += perf_clock(p) - start;

  start = perf_clock(p);
  glfwSwapBuffers(application->window);
  if (p->enabled) p->frame.swap += perf_clock(p) - start;
}

#pragma endregion

/* Raw duration is the previous start-to-start interval, before simulation clamping.
   Work fields describe this frame; a spike appears in raw on the next frame. */
int application_frame(Application *const a) {
  Performance *const p = &a->performance;
  p->frame = (FrameWork){0};
  double start = perf_clock(p);
  application_update(a);
  if (p->enabled) {
    p->frame.raw = a->rawFrame;
    p->frame.update = perf_clock(p) - start;
  }

  start = perf_clock(p);
  const StreamResult result = application_stream_step(&a->streaming, &a->renderer, game_streaming_world(&a->game), game_player(&a->game));
  if (p->enabled) p->frame.sync = perf_clock(p) - start;

  application_render(a);
  perf_record(p);
  /* Failed transactional work remains queued for retry next frame. */
  return result != STREAM_FAILED;
}

#pragma region Main
int application_run(void) {
  Application application;
  if (!application_init(&application))
    return EXIT_FAILURE;

  int meshRebuildFailed = 0;
  while (!glfwWindowShouldClose(application.window)) {
    if (!application_frame(&application)) {
      if (!meshRebuildFailed)
        fprintf(stderr, "Failed to rebuild dirty section meshes; retaining previous "
                        "meshes and retrying\n");
      meshRebuildFailed = 1;
    } else {
      meshRebuildFailed = 0;
    }
    Performance *const p = &application.performance;
    if (p->enabled && (p->count == PERF_SAMPLES || p->total.raw >= 1.0)) {
      performance_report(&application, "play");
      performance_reset(p);
    }
  }
  if (application.performance.count) performance_report(&application, "play-final");

  application_shutdown(&application);
  return EXIT_SUCCESS;
}
#pragma endregion

/* View distance requests the local cache; server interest can supply a different
   region directly to world_stream_update without depending on the renderer. */
StreamResult application_stream_view(WorldStreaming *const stream, Renderer *const renderer,
                                     World *const world, const Player *const player,
                                     const StreamBudget *const budget) {
  StreamRegion region = {0};
  const float requested = renderer_view_distance(renderer);
  const float radius = isfinite(requested) && requested > 0.0f ?
      fminf(requested, MAX_VIEW_DISTANCE) : DEFAULT_VIEW_DISTANCE;
  if (world->proceduralTerrain &&
      !world_stream_region(world, player_eye_position(player), radius, &region)) return STREAM_FAILED;
  const StreamConsumer consumer = renderer_stream_consumer(renderer);
  return world_stream_update(stream, world, region, budget, &consumer, renderer->performance);
}

StreamResult application_stream_step(WorldStreaming *const stream, Renderer *const renderer,
                                     World *const world, const Player *const player) {
  const StreamBudget budget = {.seconds = 0.002, .operations = 128, .bytes = 2 * 1024 * 1024};
  return application_stream_view(stream, renderer, world, player, &budget);
}
