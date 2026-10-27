/* Drive the real application update and mouse callbacks without an X11/GL context. */
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

#define glfwGetKey __wrap_glfwGetKey
#define glfwGetMouseButton __wrap_glfwGetMouseButton
#define glfwGetInputMode __wrap_glfwGetInputMode
#define glfwSetInputMode __wrap_glfwSetInputMode
#define glfwGetTime __wrap_glfwGetTime
#define glfwGetWindowUserPointer __wrap_glfwGetWindowUserPointer
#define glfwGetCursorPos __wrap_glfwGetCursorPos
#define glfwGetWindowSize __wrap_glfwGetWindowSize
#define glfwGetFramebufferSize __wrap_glfwGetFramebufferSize
#define glfwSetWindowShouldClose __wrap_glfwSetWindowShouldClose
#define glfwPollEvents __wrap_glfwPollEvents
#include "renderer_test.h"
/* Compile the UI with the same GLFW stubs and inspect its shared layout. */
#include "../ui.c"
#include "../input.c"

void __wrap_glfwPollEvents(void) {
  if (pendingClick) {
    int button = pendingClick - 1;
    pendingClick = 0;
    mouse_button_callback(((Application *)windowUser)->window, button, GLFW_PRESS, 0);
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
  assert(actual->camera.yaw == expected->camera.yaw);
  assert(actual->camera.pitch == expected->camera.pitch);
  assert(actual->camera.front.x == expected->camera.front.x);
  assert(actual->camera.front.y == expected->camera.front.y);
  assert(actual->camera.front.z == expected->camera.front.z);
  assert(actual->camera.up.x == expected->camera.up.x);
  assert(actual->camera.up.y == expected->camera.up.y);
  assert(actual->camera.up.z == expected->camera.up.z);
}

/* Find the actual displayed control by label; layout changes must not rewrite scenarios. */
static void click_button(Application *application, const char *label) {
  MenuButton buttons[3];
  const int count = menu_buttons(application, buttons);
  const float scale = fminf((float)windowWidth / 640.0f, (float)windowHeight / 480.0f);
  for (int i = 0; i < count; i++)
    if (strcmp(buttons[i].label, label) == 0) {
      click(application, windowWidth * 0.5 + buttons[i].centerX * scale,
              windowHeight * 0.5 + (buttons[i].top + 24) * scale);
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
  windowUser = &application;
  cursorMode = GLFW_CURSOR_DISABLED;
  mouse_button_callback(NULL, GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
  mouse_button_callback(NULL, GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
  input_poll(&application.input, NULL);
  assert(input_player(&application.input).jump);
  input_poll(&application.input, NULL);
  assert(!input_player(&application.input).jump);
  mouse_button_callback(NULL, GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
  input_discard(&application.input);
  input_poll(&application.input, NULL);
  assert(!input_player(&application.input).jump);
  memset(keys, 0, sizeof(keys));
}

static void test_view_distance_settings(int width, int height, int density) {
  windowWidth = width; windowHeight = height; pixelScale = density;
  Application application = {0};
  input_init(&application.input);
  application.input.keyBindings[GLFW_KEY_ESCAPE] = 0;
  application.input.keyBindings[GLFW_KEY_F1] = INPUT_PAUSE;
  windowUser = &application;
  memset(keys, 0, sizeof(keys));
  closeRequested = 0;
  now = 0;
  application.renderer.viewDistance = DEFAULT_VIEW_DISTANCE;
  application.player = (Player){.position = {0, 5, 0}, .eyeHeight = 1,
      .camera = {.front = {0, 0, -1}, .up = {0, 1, 0}, .yaw = -90}};
  assert(create_platform(&application.world, 16, (Vec3){0}, 1, 1));
  /* Keep editable terrain in reach: a leaked gameplay click must have an effect. */
  assert(world_set_block(&application.world, 0, 5, -3, BLOCK_TYPE_STONE));
  assert(world_set_block(&application.world, 0, 6, -3, BLOCK_TYPE_STONE));
  keys[GLFW_KEY_F1] = GLFW_PRESS;
  frame(&application, 0.02);
  keys[GLFW_KEY_F1] = GLFW_RELEASE;
  frame(&application, 0.02);
  Player frozen = application.player;
  size_t voxels = application.world.voxelCount;
  click_button(&application, "SETTINGS"); /* Settings */
  assert(application.paused && application.settingsOpen);

  float beyond = DEFAULT_VIEW_DISTANCE + VIEW_DISTANCE_STEP * 0.5f;
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
  int steps = (int)ceilf((MAX_VIEW_DISTANCE - MIN_VIEW_DISTANCE) / VIEW_DISTANCE_STEP) + 2;
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
  assert(application.world.voxelCount == voxels);
  assert_player_unchanged(&application.player, &frozen);

  click_button(&application, "BACK"); /* Back, not Quit */
  assert(application.paused && !application.settingsOpen && !closeRequested);
  click_button(&application, "SETTINGS");
  assert(application.settingsOpen && application.renderer.viewDistance == MIN_VIEW_DISTANCE);
  keys[GLFW_KEY_F1] = GLFW_PRESS;
  frame(&application, 0.02);
  assert(application.paused && !application.settingsOpen);
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
  assert(application.settingsOpen && application.renderer.viewDistance == MIN_VIEW_DISTANCE);
  world_destroy(&application.world);
}

int main(void) {
  test_virtual_actions();
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
  windowUser = &application;
  assert(create_platform(&application.world, 16, (Vec3){0}, 1, 1));
  /* Keep editable terrain in reach: a leaked gameplay click must have an effect. */
  assert(world_set_block(&application.world, 0, 5, -3, BLOCK_TYPE_STONE));
  assert(world_set_block(&application.world, 0, 6, -3, BLOCK_TYPE_STONE));
  application.player = (Player){.position = {0, 5, 0}, .eyeHeight = 1,
      .camera = {.front = {0, 0, -1}, .up = {0, 1, 0}, .yaw = -90}};
  keys[GLFW_KEY_W] = GLFW_PRESS;
  frame(&application, 0.05);
  assert(application.player.position.z < 0 && application.player.position.y < 5);

  keys[GLFW_KEY_ESCAPE] = GLFW_PRESS;
  frame(&application, 0.02);
  assert(application.paused && cursorMode == GLFW_CURSOR_NORMAL);
  Player frozen = application.player;
  size_t voxels = application.world.voxelCount;
  /* Holding Escape must not repeatedly toggle pause, even with movement held. */
  for (int i = 0; i < 30; i++)
    frame(&application, 0.1);
  assert(application.paused);
  assert_player_unchanged(&application.player, &frozen);
  keys[GLFW_KEY_ESCAPE] = GLFW_RELEASE;
  keys[GLFW_KEY_SPACE] = GLFW_PRESS;
  mouse_callback(application.window, 100, 100);
  mouse_callback(application.window, 200, 150);
  pendingClick = GLFW_MOUSE_BUTTON_RIGHT + 1;
  frame(&application, 10);
  assert(fabs(application.rawFrame - 10) < 1e-9);
  assert(application.world.voxelCount == voxels);
  assert_player_unchanged(&application.player, &frozen);
  /* Click outside the buttons: remain paused and do not place anything. */
  cursorX = 10; cursorY = 10;
  pendingClick = GLFW_MOUSE_BUTTON_LEFT + 1;
  frame(&application, 0.02);
  assert(application.paused && application.world.voxelCount == voxels);

  /* Resume with the mouse. The resume click and elapsed pause cannot affect gameplay. */
  click_button(&application, "RESUME");
  assert(!application.paused && cursorMode == GLFW_CURSOR_DISABLED);
  assert_player_unchanged(&application.player, &frozen);
  assert(application.world.voxelCount == voxels);
  assert(!application.jumpQueued && !application.placeQueued && !application.removeQueued);
  frame(&application, 0.02);
  assert(application.player.position.z < frozen.position.z);
  assert(application.player.position.y < frozen.position.y);

  /* Escape also resumes, with no movement on the transition frame. */
  keys[GLFW_KEY_ESCAPE] = GLFW_PRESS;
  frame(&application, 0.02);
  assert(application.paused);
  keys[GLFW_KEY_ESCAPE] = GLFW_RELEASE;
  frame(&application, 0.02);
  frozen = application.player;
  keys[GLFW_KEY_ESCAPE] = GLFW_PRESS;
  frame(&application, 0.02);
  assert(!application.paused && cursorMode == GLFW_CURSOR_DISABLED);
  assert_player_unchanged(&application.player, &frozen);

  keys[GLFW_KEY_ESCAPE] = GLFW_RELEASE;
  frame(&application, 0.02);
  keys[GLFW_KEY_ESCAPE] = GLFW_PRESS;
  frame(&application, 0.02);
  assert(application.paused);
  click_button(&application, "QUIT");
  assert(closeRequested);
  world_destroy(&application.world);
  test_view_distance_settings(640, 480, 1);
  test_view_distance_settings(1280, 720, 2);
  test_view_distance_settings(480, 800, 2);
  puts("All pause menu interaction tests passed.");
  return 0;
}
