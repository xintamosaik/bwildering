/* Drive the real application update and mouse callbacks without an X11/GL context. */
#include "../world_generation.h"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <assert.h>
#include <string.h>

static int keys[GLFW_KEY_LAST + 1];
static int mouseButtons[GLFW_MOUSE_BUTTON_LAST + 1];
static int cursorMode = GLFW_CURSOR_DISABLED;
static double now, cursorX, cursorY;
static int closeRequested, pendingClick;
static int windowWidth = 640, windowHeight = 480, pixelScale = 1;
static void *windowUser;
int __wrap_glfwGetKey(GLFWwindow *window, int key) { (void)window; return keys[key]; }
int __wrap_glfwGetMouseButton(GLFWwindow *const window, const int button) {
  (void)window;
  return mouseButtons[button];
}
int __wrap_glfwGetInputMode(GLFWwindow *window, int mode) {
  (void)window; (void)mode; return cursorMode;
}
void __wrap_glfwSetInputMode(GLFWwindow *window, int mode, int value) {
  (void)window; (void)mode; cursorMode = value;
}
double __wrap_glfwGetTime(void) { return now; }
void *__wrap_glfwGetWindowUserPointer(GLFWwindow *window) { (void)window; return windowUser; }
void __wrap_glfwGetCursorPos(GLFWwindow *window, double *x, double *y) {
  (void)window; *x = cursorX; *y = cursorY;
}
void __wrap_glfwGetWindowSize(GLFWwindow *window, int *width, int *height) {
  (void)window; *width = windowWidth; *height = windowHeight;
}
void __wrap_glfwGetFramebufferSize(GLFWwindow *window, int *width, int *height) {
  __wrap_glfwGetWindowSize(window, width, height);
  *width *= pixelScale; *height *= pixelScale;
}
void __wrap_glfwSetWindowShouldClose(GLFWwindow *window, int value) {
  (void)window; closeRequested = value;
}
void __wrap_glfwPollEvents(void);

#include "renderer_test.h"
static GLFWmousebuttonfun buttonCallback;
static GLFWcursorposfun cursorCallback;
static GLFWkeyfun keyCallback;
static GLFWframebuffersizefun framebufferCallback;
GLFWframebuffersizefun __wrap_glfwSetFramebufferSizeCallback(
    GLFWwindow *const window, GLFWframebuffersizefun const callback) {
  (void)window; framebufferCallback = callback; return NULL;
}
void __wrap_glfwSetWindowUserPointer(GLFWwindow *const window, void *const pointer) {
  (void)window; windowUser = pointer;
}
GLFWmousebuttonfun __wrap_glfwSetMouseButtonCallback(GLFWwindow *const window, GLFWmousebuttonfun const callback) {
  (void)window; buttonCallback = callback; return NULL;
}
GLFWcursorposfun __wrap_glfwSetCursorPosCallback(GLFWwindow *const window, GLFWcursorposfun const callback) {
  (void)window; cursorCallback = callback; return NULL;
}
GLFWkeyfun __wrap_glfwSetKeyCallback(GLFWwindow *const window, GLFWkeyfun const callback) {
  (void)window; keyCallback = callback; return NULL;
}

void __wrap_glfwPollEvents(void) {
  if (pendingClick) {
    const int button = pendingClick - 1;
    pendingClick = 0;
    buttonCallback(NULL, button, GLFW_PRESS, 0);
  }
}

static void frame(Application *application, double elapsed) {
  now += elapsed;
  application_update(application);
}

static void click(Application *application, double x, double y) {
  cursorX = x;
  cursorY = y;
  pendingClick = GLFW_MOUSE_BUTTON_LEFT + 1;
  frame(application, 0.02);
}

static void assert_player_unchanged(const Player *actual, const Player *expected) {
  assert(actual->position.x == expected->position.x);
  assert(actual->position.y == expected->position.y);
  assert(actual->position.z == expected->position.z);
  assert(actual->velocityY == expected->velocityY);
  assert(actual->grounded == expected->grounded);
  assert(actual->eyeHeight == expected->eyeHeight);
  assert(actual->crouching == expected->crouching);
  assert(actual->aim.yaw == expected->aim.yaw);
  assert(actual->aim.pitch == expected->aim.pitch);
  assert(actual->aim.front.x == expected->aim.front.x);
  assert(actual->aim.front.y == expected->aim.front.y);
  assert(actual->aim.front.z == expected->aim.front.z);
}

/* Exercise the visible controls through captured clicks and the public routing path. */
static void click_button(Application *application, const char *label) {
  UiButton buttons[UI_BUTTON_CAPACITY];
  const int count = ui_menu_buttons(&application->ui,
      renderer_view_distance(&application->renderer), buttons);
  const float scale = fminf((float)windowWidth / 640.0f, (float)windowHeight / 480.0f);
  for (int i = 0; i < count; i++)
    if (strcmp(buttons[i].label, label) == 0) {
      click(application, windowWidth * 0.5 + buttons[i].centerX * scale,
              windowHeight * 0.5 + (buttons[i].top + 24.0f) * scale);
      return;
    }
  assert(!"Requested menu control is not displayed");
}

/* Bindings aggregate physical sources before detecting virtual action edges. */
static void test_virtual_actions(void) {
  InputState input;
  input_init(&input);
  memset(keys, 0, sizeof(keys));
  memset(mouseButtons, 0, sizeof(mouseButtons));
  input.keyBindings[GLFW_KEY_W] = 0;
  input.keyBindings[GLFW_KEY_UP] = INPUT_FORWARD;
  input.keyBindings[GLFW_KEY_DOWN] = INPUT_BACKWARD;
  keys[GLFW_KEY_W] = GLFW_PRESS;
  input_poll(&input, NULL);
  assert(!input.held && !input.pressed);
  keys[GLFW_KEY_UP] = GLFW_PRESS;
  input_poll(&input, NULL);
  assert(input.held == INPUT_FORWARD && input.pressed == INPUT_FORWARD);
  assert(input_player(&input).forward == 1.0f);
  input_poll(&input, NULL);
  assert(!input.pressed && input.held == INPUT_FORWARD);
  keys[GLFW_KEY_DOWN] = GLFW_PRESS;
  keys[GLFW_KEY_A] = GLFW_PRESS;
  keys[GLFW_KEY_D] = GLFW_PRESS;
  input_poll(&input, NULL);
  assert(input_player(&input).forward == 0.0f);
  assert(input_player(&input).strafe == 0.0f);

  keys[GLFW_KEY_LEFT_CONTROL] = GLFW_PRESS;
  input_poll(&input, NULL);
  assert(input.pressed & INPUT_RUN);
  keys[GLFW_KEY_RIGHT_CONTROL] = GLFW_PRESS;
  input_poll(&input, NULL);
  assert(!(input.pressed & INPUT_RUN));
  keys[GLFW_KEY_LEFT_CONTROL] = GLFW_RELEASE;
  input_poll(&input, NULL);
  assert(input_player(&input).run);
  assert(!(input.pressed & INPUT_RUN));
  keys[GLFW_KEY_RIGHT_CONTROL] = GLFW_RELEASE;
  input_poll(&input, NULL);
  assert(!input_player(&input).run);

  /* A key can activate several actions; mouse buttons use the same virtual map. */
  input.keyBindings[GLFW_KEY_J] = INPUT_JUMP | INPUT_CROUCH;
  input.mouseBindings[GLFW_MOUSE_BUTTON_LEFT] = INPUT_JUMP;
  keys[GLFW_KEY_J] = GLFW_PRESS;
  input_poll(&input, NULL);
  assert(input_player(&input).jump && input_player(&input).crouch);
  mouseButtons[GLFW_MOUSE_BUTTON_LEFT] = GLFW_PRESS;
  input_poll(&input, NULL);
  assert(!input_player(&input).jump);
  keys[GLFW_KEY_J] = GLFW_RELEASE;
  input_poll(&input, NULL);
  assert((input.held & INPUT_JUMP) && !input_player(&input).jump);
  mouseButtons[GLFW_MOUSE_BUTTON_LEFT] = GLFW_RELEASE;
  input_poll(&input, NULL);
  assert(!(input.held & INPUT_JUMP));

  Application application = {0};
  input_init(&application.input);
  application.input.mouseBindings[GLFW_MOUSE_BUTTON_LEFT] = INPUT_JUMP;
  input_attach(&application.input, NULL);
  cursorMode = GLFW_CURSOR_DISABLED;
  buttonCallback(NULL, GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
  buttonCallback(NULL, GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
  input_poll(&application.input, NULL);
  assert(input_player(&application.input).jump);
  input_poll(&application.input, NULL);
  assert(!input_player(&application.input).jump);
  buttonCallback(NULL, GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
  input_discard(&application.input);
  input_poll(&application.input, NULL);
  assert(!input_player(&application.input).jump);
  memset(keys, 0, sizeof(keys));
}

/* Callback capture cannot mutate menus, timing, cursor capture or the world. */
static void test_passive_capture_and_ordering(void) {
  Application application = {0};
  input_init(&application.input);
  input_attach(&application.input, NULL);
  memset(keys, 0, sizeof(keys));
  memset(mouseButtons, 0, sizeof(mouseButtons));
  cursorMode = GLFW_CURSOR_DISABLED;
  closeRequested = 0;
  windowWidth = 640; windowHeight = 480; pixelScale = 2;
  application.renderer.viewDistance = DEFAULT_VIEW_DISTANCE;
  application.game.player = (Player){.position = {0, 5, 0}, .eyeHeight = 1,
      .aim = {.front = {0, 0, -1}, .yaw = -90}};
  application.pendingAim = application.game.player.aim;
  assert(create_platform(&application.game.world, 16, (Vec3){0}, 1, 1));
  assert(world_set_block(&application.game.world, 0, 5, -3, BLOCK_TYPE_STONE));
  const Player frozen = application.game.player;
  const size_t voxels = application.game.world.voxelCount;
  application.simulationAccumulator = 0.01;
  application.jumpQueued = application.placeQueued = application.removeQueued = 1;
  application.input.keyBindings[GLFW_KEY_P] = INPUT_PAUSE | INPUT_JUMP | INPUT_PLACE;
  keyCallback(NULL, GLFW_KEY_P, 0, GLFW_PRESS, 0);
  keyCallback(NULL, GLFW_KEY_P, 0, GLFW_RELEASE, 0);
  cursorCallback(NULL, 100.0, 100.0);
  cursorCallback(NULL, 200.0, 150.0);
  assert(!application.paused && application.simulationAccumulator == 0.01);
  assert(cursorMode == GLFW_CURSOR_DISABLED && !closeRequested);
  /* Resizing stays passive and cannot be lost when pause discards gameplay. */
  framebufferCallback(NULL, 1280, 960);
  assert(application.renderer.framebufferWidth == 0);
  frame(&application, 0.02);
  assert(application.input.framebufferResized);
  assert(application.input.framebufferWidth == 1280 && application.input.framebufferHeight == 960);
  assert(application.paused && !application.ui.settingsOpen);
  assert(!application.simulationAccumulator && !application.jumpQueued);
  assert(!application.placeQueued && !application.removeQueued);
  assert(!application.input.mouse.deltaX && !application.input.mouse.deltaY);
  assert(application.game.world.voxelCount == voxels);
  assert_player_unchanged(&application.game.player, &frozen);

  /* All clicks happen between polls; the final cursor position is unrelated.
     Settings, increase, back and resume must each use its captured coordinates. */
  const double positions[][2] = {{320, 240}, {436, 240}, {320, 304},
                                  {320, 176}, {320, 304}};
  for (size_t i = 0; i < sizeof(positions) / sizeof(positions[0]); i++) {
    cursorX = positions[i][0]; cursorY = positions[i][1];
    buttonCallback(NULL, GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
    buttonCallback(NULL, GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
  }
  cursorX = 0; cursorY = 0;
  assert(!application.ui.settingsOpen && application.paused);
  assert(application.renderer.viewDistance == DEFAULT_VIEW_DISTANCE);
  frame(&application, 0.02);
  assert(!application.paused && !application.ui.settingsOpen && !closeRequested);
  assert(application.renderer.viewDistance == DEFAULT_VIEW_DISTANCE + VIEW_DISTANCE_STEP);
  assert(!application.input.clickCount && !application.input.pressed);
  assert(application.game.world.voxelCount == voxels);
  assert_player_unchanged(&application.game.player, &frozen);

  /* Pause beats a simultaneous resume click and clears gameplay actions. */
  application.input.mouseBindings[GLFW_MOUSE_BUTTON_RIGHT] = INPUT_PAUSE | INPUT_REMOVE;
  buttonCallback(NULL, GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS, 0);
  cursorX = 320; cursorY = 176;
  buttonCallback(NULL, GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
  frame(&application, 0.02);
  assert(application.paused && !application.input.clickCount);
  assert(application.game.world.voxelCount == voxels);

  /* Overflow drops newest menu clicks; coalesced gameplay capture continues. */
  cursorX = 0; cursorY = 0;
  for (int i = 0; i < INPUT_CLICK_CAPACITY; i++)
    buttonCallback(NULL, GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
  cursorX = 320; cursorY = 176;
  buttonCallback(NULL, GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
  assert(application.input.clickCount == INPUT_CLICK_CAPACITY);
  frame(&application, 0.02);
  assert(application.paused && !application.input.clickCount);
  click_button(&application, "RESUME");
  assert(!application.paused);
  assert_player_unchanged(&application.game.player, &frozen);
  world_destroy(&application.game.world);
  pixelScale = 1;
}

static void test_view_distance_settings(int width, int height, int density) {
  windowWidth = width; windowHeight = height; pixelScale = density;
  Application application = {0};
  input_init(&application.input);
  application.input.keyBindings[GLFW_KEY_ESCAPE] = 0;
  application.input.keyBindings[GLFW_KEY_F1] = INPUT_PAUSE;
  input_attach(&application.input, NULL);
  memset(keys, 0, sizeof(keys));
  closeRequested = 0;
  now = 0;
  application.renderer.viewDistance = DEFAULT_VIEW_DISTANCE;
  application.game.player = (Player){.position = {0, 5, 0}, .eyeHeight = 1,
      .aim = {.front = {0, 0, -1}, .yaw = -90}};
  application.pendingAim = application.game.player.aim;
  assert(create_platform(&application.game.world, 16, (Vec3){0}, 1, 1));
  /* Keep editable terrain in reach: a leaked gameplay click must have an effect. */
  assert(world_set_block(&application.game.world, 0, 5, -3, BLOCK_TYPE_STONE));
  assert(world_set_block(&application.game.world, 0, 6, -3, BLOCK_TYPE_STONE));
  keys[GLFW_KEY_F1] = GLFW_PRESS;
  frame(&application, 0.02);
  keys[GLFW_KEY_F1] = GLFW_RELEASE;
  frame(&application, 0.02);
  Player frozen = application.game.player;
  const size_t voxels = application.game.world.voxelCount;
  click_button(&application, "SETTINGS"); /* Settings */
  assert(application.paused && application.ui.settingsOpen);

  const float beyond = DEFAULT_VIEW_DISTANCE + VIEW_DISTANCE_STEP * 0.5f;
  SectionGpuMesh distant = {.boundsMin = {beyond, -8, -8}, .boundsMax = {beyond + 16, 8, 8}};
  SectionGpuMesh nearby = {.boundsMin = {-8, -8, -8}, .boundsMax = {8, 8, 8}};
  assert(!section_within_view_distance(&distant, (Vec3){0}, application.renderer.viewDistance));
  click_button(&application, "+"); /* Increase */
  assert(application.renderer.viewDistance == DEFAULT_VIEW_DISTANCE + VIEW_DISTANCE_STEP);
  assert(section_within_view_distance(&distant, (Vec3){0}, application.renderer.viewDistance));
  assert(section_within_view_distance(&nearby, (Vec3){0}, application.renderer.viewDistance));
  click_button(&application, "-"); /* Decrease */
  assert(application.renderer.viewDistance == DEFAULT_VIEW_DISTANCE);
  assert(!section_within_view_distance(&distant, (Vec3){0}, application.renderer.viewDistance));
  const int steps = (int)ceilf((MAX_VIEW_DISTANCE - MIN_VIEW_DISTANCE) / VIEW_DISTANCE_STEP) + 2;
  for (int i = 0; i < steps; i++)
    click_button(&application, "+");
  assert(application.renderer.viewDistance == MAX_VIEW_DISTANCE);
  for (int i = 0; i < steps; i++)
    click_button(&application, "-");
  assert(application.renderer.viewDistance == MIN_VIEW_DISTANCE);
  assert(section_within_view_distance(&nearby, (Vec3){0}, application.renderer.viewDistance));
  /* Visibility follows the camera as it moves, rather than the world origin. */
  assert(section_within_view_distance(&distant, (Vec3){beyond - 10, 0, 0}, 25));
  assert(!section_within_view_distance(&distant, (Vec3){beyond - 10, 50, 0}, 25));
  assert(application.paused && !closeRequested);
  assert(application.game.world.voxelCount == voxels);
  assert_player_unchanged(&application.game.player, &frozen);

  click_button(&application, "BACK"); /* Back, not Quit */
  assert(application.paused && !application.ui.settingsOpen && !closeRequested);
  click_button(&application, "SETTINGS");
  assert(application.ui.settingsOpen && application.renderer.viewDistance == MIN_VIEW_DISTANCE);
  keys[GLFW_KEY_F1] = GLFW_PRESS;
  frame(&application, 0.02);
  assert(application.paused && !application.ui.settingsOpen);
  keys[GLFW_KEY_F1] = GLFW_RELEASE;
  frame(&application, 0.02);
  keys[GLFW_KEY_F1] = GLFW_PRESS;
  frame(&application, 0.02);
  assert(!application.paused && cursorMode == GLFW_CURSOR_DISABLED);
  assert(application.renderer.viewDistance == MIN_VIEW_DISTANCE);
  keys[GLFW_KEY_F1] = GLFW_RELEASE;
  frame(&application, 0.02);
  keys[GLFW_KEY_F1] = GLFW_PRESS;
  frame(&application, 0.02);
  click_button(&application, "SETTINGS");
  assert(application.ui.settingsOpen && application.renderer.viewDistance == MIN_VIEW_DISTANCE);
  world_destroy(&application.game.world);
}

/* Pending local look renders immediately without advancing authoritative aim. */
static void test_pending_game_view(void) {
  Application application = {0};
  input_init(&application.input);
  input_attach(&application.input, NULL);
  memset(keys, 0, sizeof(keys));
  memset(mouseButtons, 0, sizeof(mouseButtons));
  cursorMode = GLFW_CURSOR_DISABLED;
  pendingClick = 0;
  now = 0.0;
  assert(create_platform(&application.game.world, 16, (Vec3){0}, 1.0f, 1));
  application.game.player = (Player){.position = {0.5f, 5.0f, 0.5f},
      .eyeHeight = 1.0f, .aim = game_aim(-90.0f, 0.0f)};
  application.pendingAim = application.game.player.aim;
  const Player frozen = application.game.player;
  cursorCallback(NULL, 0.0, 0.0);
  cursorCallback(NULL, 900.0, 0.0);
  frame(&application, 0.001);
  assert(application.game.tick == 0);
  assert_player_unchanged(&application.game.player, &frozen);
  const View view = player_view(game_player(&application.game), application.pendingAim);
  assert(view.front.x > 0.99f && frozen.aim.front.z < -0.99f);
  frame(&application, 0.02);
  assert(application.game.tick == 1 && application.game.player.aim.front.x > 0.99f);
  cursorCallback(NULL, 1000.0, 0.0);
  frame(&application, 0.001);
  assert(application.pendingAim.yaw > application.game.player.aim.yaw);
  keys[GLFW_KEY_ESCAPE] = GLFW_PRESS;
  frame(&application, 0.001);
  assert(application.paused && application.game.tick == 1);
  assert(application.pendingAim.yaw == application.game.player.aim.yaw);
  keys[GLFW_KEY_ESCAPE] = GLFW_RELEASE;
  frame(&application, 10.0);
  assert(application.game.tick == 1 && application.simulationAccumulator == 0.0);
  keys[GLFW_KEY_ESCAPE] = GLFW_PRESS;
  frame(&application, 10.0);
  assert(!application.paused && application.game.tick == 1);
  assert(application.simulationAccumulator == 0.0);
  keys[GLFW_KEY_ESCAPE] = GLFW_RELEASE;
  /* The first recaptured position seeds the mouse instead of producing a
     jump from the old cursor location. Resume does not accumulate pause debt. */
  cursorCallback(NULL, 5000.0, 5000.0);
  frame(&application, 0.001);
  assert(application.game.tick == 1);
  assert(application.pendingAim.yaw == application.game.player.aim.yaw);
  frame(&application, 0.02);
  assert(application.game.tick == 2);
  memset(keys, 0, sizeof(keys));
  game_destroy(&application.game);
}

/* Real frame scheduler versus explicitly assigned commands. Two targets reveal
   replayed removes; simultaneous place/remove must leave the original intact. */
static void test_tick_consumption(void) {
  Application application = {0};
  Game expected = {0};
  input_init(&application.input);
  input_attach(&application.input, NULL);
  memset(keys, 0, sizeof(keys));
  memset(mouseButtons, 0, sizeof(mouseButtons));
  cursorMode = GLFW_CURSOR_DISABLED;
  pendingClick = 0;
  now = 0.0;
  Game *const actual = &application.game;
  Game *const games[] = {actual, &expected};
  for (size_t i = 0; i < sizeof(games) / sizeof(*games); i++) {
    Game *const game = games[i];
    assert(world_init_region(&game->world, -8, -8, 16, (Vec3){0}, 1.0f));
    for (int x = -8; x < 8; x++)
      for (int z = -8; z < 8; z++)
        assert(world_set_block(&game->world, x, -1, z, BLOCK_TYPE_STONE));
    game->player = (Player){.position = {0.5f, 0.0f, 0.5f}, .grounded = 1,
        .eyeHeight = 1.0f, .aim = game_aim(-90.0f, 0.0f)};
    assert(world_set_block(&game->world, 3, 1, 0, BLOCK_TYPE_GRASS));
    assert(world_set_block(&game->world, 4, 1, 0, BLOCK_TYPE_SAND));
  }
  application.pendingAim = actual->player.aim;
  cursorCallback(NULL, 0.0, 0.0);
  /* Press/release twice, across separate zero-tick frames. Latest aim and
     held state at consumption win, even though the presses happened earlier. */
  for (int i = 0; i < 2; i++) {
    keyCallback(NULL, GLFW_KEY_SPACE, 0, GLFW_PRESS, 0);
    keyCallback(NULL, GLFW_KEY_SPACE, 0, GLFW_RELEASE, 0);
    buttonCallback(NULL, GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
    buttonCallback(NULL, GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS, 0);
    cursorCallback(NULL, i == 0 ? 450.0 : 900.0, 0.0);
    frame(&application, 0.001);
    assert(actual->tick == 0 && actual->player.aim.yaw == -90.0f);
    assert(application.jumpQueued && application.placeQueued && application.removeQueued);
    assert(world_get_block(&actual->world, 2, 1, 0) == BLOCK_TYPE_EMPTY);
    assert(world_get_block(&actual->world, 3, 1, 0) == BLOCK_TYPE_GRASS);
  }
  keys[GLFW_KEY_W] = keys[GLFW_KEY_LEFT_CONTROL] = keys[GLFW_KEY_LEFT_SHIFT] = GLFW_PRESS;
  keys[GLFW_KEY_SPACE] = GLFW_PRESS;
  frame(&application, 0.05);
  assert(actual->tick == 3);
  assert(!application.jumpQueued && !application.placeQueued && !application.removeQueued);
  for (int tick = 0; tick < 3; tick++)
    game_step(&expected, (GameCommand){.yaw = 0.0f,
        .movement = {.forward = 1.0f, .run = 1, .crouch = 1, .jump = tick == 0},
        .place = tick == 0, .remove = tick == 0, .material = BLOCK_TYPE_STONE});
  assert_player_unchanged(&actual->player, &expected.player);
  assert(actual->player.position.x > 0.5f && !actual->player.grounded);
  assert(world_get_block(&actual->world, 2, 1, 0) == BLOCK_TYPE_EMPTY);
  assert(world_get_block(&actual->world, 3, 1, 0) == BLOCK_TYPE_GRASS);

  /* Remove once despite several catch-up ticks and repeated pending presses. */
  buttonCallback(NULL, GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS, 0);
  buttonCallback(NULL, GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS, 0);
  frame(&application, 0.05);
  assert(actual->tick == 6);
  for (int tick = 0; tick < 3; tick++)
    game_step(&expected, (GameCommand){.yaw = 0.0f,
        .movement = {.forward = 1.0f, .run = 1, .crouch = 1}, .remove = tick == 0});
  assert_player_unchanged(&actual->player, &expected.player);
  assert(world_get_block(&actual->world, 3, 1, 0) == BLOCK_TYPE_EMPTY);
  assert(world_get_block(&actual->world, 4, 1, 0) == BLOCK_TYPE_SAND);

  /* A long render frame contributes only 0.25 seconds; raw timing retains it. */
  frame(&application, 10.0);
  assert(actual->tick == 21 && fabs(application.rawFrame - 10.0) < 1e-9);
  assert(application.simulationAccumulator >= 0.0);
  assert(application.simulationAccumulator < 1.0 / 60.0);
  for (int tick = 0; tick < 15; tick++)
    game_step(&expected, (GameCommand){.yaw = 0.0f,
        .movement = {.forward = 1.0f, .run = 1, .crouch = 1}});
  assert_player_unchanged(&actual->player, &expected.player);
  /* Clock reversal cannot run a tick or subtract accumulated time. */
  const double remainder = application.simulationAccumulator;
  frame(&application, -1.0);
  assert(actual->tick == 21 && application.rawFrame == 0.0);
  assert(application.simulationAccumulator == remainder);
  /* Holding jump through landing must not generate another press. */
  keys[GLFW_KEY_W] = GLFW_RELEASE;
  for (int frameIndex = 0; frameIndex < 4; frameIndex++)
    frame(&application, 0.25);
  for (int tick = 0; tick < 60; tick++)
    game_step(&expected, (GameCommand){.yaw = 0.0f, .movement = {.run = 1, .crouch = 1}});
  assert(actual->tick == 81 && actual->player.grounded);
  assert_player_unchanged(&actual->player, &expected.player);
  memset(keys, 0, sizeof(keys));
  game_destroy(actual);
  game_destroy(&expected);
}

int main(void) {
  test_tick_consumption();
  test_pending_game_view();
  test_virtual_actions();
  test_passive_capture_and_ordering();
  /* Nearest-rank p95, maximum and mean retain spikes above the simulation cap. */
  Performance perf = {.enabled = 1};
  for (int i = 1; i <= 100; i++) {
    perf.frame.raw = i / 100.0;
    perf_record(&perf);
  }
  perf_finish_window(&perf);
  assert(fabs(perf.average - 0.505) < 1e-9);
  assert(perf.p95 == 0.95 && perf.maximum == 1.0);
  for (int i = 100; i < PERF_SAMPLES + 10; i++) perf_record(&perf);
  assert(perf.count == PERF_SAMPLES);
  performance_reset(&perf);
  assert(!perf.count && !perf.total.raw);
  perf.enabled = 0;
  perf_record(&perf);
  assert(!perf.count);

  Application application = {0};
  input_init(&application.input);
  input_attach(&application.input, NULL);
  assert(create_platform(&application.game.world, 16, (Vec3){0}, 1, 1));
  /* Keep editable terrain in reach: a leaked gameplay click must have an effect. */
  assert(world_set_block(&application.game.world, 0, 5, -3, BLOCK_TYPE_STONE));
  assert(world_set_block(&application.game.world, 0, 6, -3, BLOCK_TYPE_STONE));
  application.game.player = (Player){.position = {0, 5, 0}, .eyeHeight = 1,
      .aim = {.front = {0, 0, -1}, .yaw = -90}};
  application.pendingAim = application.game.player.aim;
  keys[GLFW_KEY_W] = GLFW_PRESS;
  frame(&application, 0.05);
  assert(application.game.player.position.z < 0 && application.game.player.position.y < 5);

  keys[GLFW_KEY_ESCAPE] = GLFW_PRESS;
  frame(&application, 0.02);
  assert(application.paused && cursorMode == GLFW_CURSOR_NORMAL);
  Player frozen = application.game.player;
  const size_t voxels = application.game.world.voxelCount;
  /* Holding Escape must not repeatedly toggle pause, even with movement held. */
  for (int i = 0; i < 30; i++)
    frame(&application, 0.1);
  assert(application.paused);
  assert_player_unchanged(&application.game.player, &frozen);
  keys[GLFW_KEY_ESCAPE] = GLFW_RELEASE;
  keys[GLFW_KEY_SPACE] = GLFW_PRESS;
  cursorCallback(application.window, 100, 100);
  cursorCallback(application.window, 200, 150);
  pendingClick = GLFW_MOUSE_BUTTON_RIGHT + 1;
  frame(&application, 10);
  assert(fabs(application.rawFrame - 10) < 1e-9);
  assert(application.game.world.voxelCount == voxels);
  assert_player_unchanged(&application.game.player, &frozen);
  /* Click outside the buttons: remain paused and do not place anything. */
  cursorX = 10; cursorY = 10;
  pendingClick = GLFW_MOUSE_BUTTON_LEFT + 1;
  frame(&application, 0.02);
  assert(application.paused && application.game.world.voxelCount == voxels);

  /* Resume with the mouse. The resume click and elapsed pause cannot affect gameplay. */
  click_button(&application, "RESUME");
  assert(!application.paused && cursorMode == GLFW_CURSOR_DISABLED);
  assert_player_unchanged(&application.game.player, &frozen);
  assert(application.game.world.voxelCount == voxels);
  assert(!application.jumpQueued && !application.placeQueued && !application.removeQueued);
  frame(&application, 0.02);
  assert(application.game.player.position.z < frozen.position.z);
  assert(application.game.player.position.y < frozen.position.y);

  /* Escape also resumes, with no movement on the transition frame. */
  keys[GLFW_KEY_ESCAPE] = GLFW_PRESS;
  frame(&application, 0.02);
  assert(application.paused);
  keys[GLFW_KEY_ESCAPE] = GLFW_RELEASE;
  frame(&application, 0.02);
  frozen = application.game.player;
  keys[GLFW_KEY_ESCAPE] = GLFW_PRESS;
  frame(&application, 0.02);
  assert(!application.paused && cursorMode == GLFW_CURSOR_DISABLED);
  assert_player_unchanged(&application.game.player, &frozen);

  keys[GLFW_KEY_ESCAPE] = GLFW_RELEASE;
  frame(&application, 0.02);
  keys[GLFW_KEY_ESCAPE] = GLFW_PRESS;
  frame(&application, 0.02);
  assert(application.paused);
  click_button(&application, "QUIT");
  assert(closeRequested);
  world_destroy(&application.game.world);
  test_view_distance_settings(640, 480, 1);
  test_view_distance_settings(1280, 720, 2);
  test_view_distance_settings(480, 800, 2);
  puts("All pause menu interaction tests passed.");
  return 0;
}
