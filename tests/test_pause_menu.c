/* Drive the real application update and mouse callbacks without an X11/GL context. */
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <assert.h>
#include <string.h>

static int keys[GLFW_KEY_LAST + 1];
static int cursorMode = GLFW_CURSOR_DISABLED;
static double now, cursorX, cursorY;
static int closeRequested, pendingClick;
static int windowWidth = 640, windowHeight = 480, pixelScale = 1;
static void *windowUser;
static int test_get_key(GLFWwindow *window, int key) { (void)window; return keys[key]; }
static int test_get_input_mode(GLFWwindow *window, int mode) {
  (void)window; (void)mode; return cursorMode;
}
static void test_set_input_mode(GLFWwindow *window, int mode, int value) {
  (void)window; (void)mode; cursorMode = value;
}
static double test_get_time(void) { return now; }
static void *test_get_user(GLFWwindow *window) { (void)window; return windowUser; }
static void test_get_cursor(GLFWwindow *window, double *x, double *y) {
  (void)window; *x = cursorX; *y = cursorY;
}
static void test_get_size(GLFWwindow *window, int *width, int *height) {
  (void)window; *width = windowWidth; *height = windowHeight;
}
static void test_get_framebuffer_size(GLFWwindow *window, int *width, int *height) {
  test_get_size(window, width, height);
  *width *= pixelScale; *height *= pixelScale;
}
static void test_set_close(GLFWwindow *window, int value) {
  (void)window; closeRequested = value;
}
static void test_poll_events(void);

#define glfwGetKey test_get_key
#define glfwGetInputMode test_get_input_mode
#define glfwSetInputMode test_set_input_mode
#define glfwGetTime test_get_time
#define glfwGetWindowUserPointer test_get_user
#define glfwGetCursorPos test_get_cursor
#define glfwGetWindowSize test_get_size
#define glfwGetFramebufferSize test_get_framebuffer_size
#define glfwSetWindowShouldClose test_set_close
#define glfwPollEvents test_poll_events
#define main bwildering_application_main
#include "../main.c"
#undef main

static void test_poll_events(void) {
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
  int count = menu_buttons(application, buttons);
  float scale = fminf(windowWidth / 640.0f, windowHeight / 480.0f);
  for (int i = 0; i < count; i++)
    if (strcmp(buttons[i].label, label) == 0) {
      click(application, windowWidth * 0.5 + buttons[i].centerX * scale,
              windowHeight * 0.5 + (buttons[i].top + 24) * scale);
      return;
    }
  assert(!"Requested menu control is not displayed");
}

static void test_view_distance_settings(int width, int height, int density) {
  windowWidth = width; windowHeight = height; pixelScale = density;
  Application application = {0};
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
  keys[GLFW_KEY_ESCAPE] = GLFW_PRESS;
  frame(&application, 0.02);
  keys[GLFW_KEY_ESCAPE] = GLFW_RELEASE;
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
  keys[GLFW_KEY_ESCAPE] = GLFW_PRESS;
  frame(&application, 0.02);
  assert(application.paused && !application.settingsOpen);
  keys[GLFW_KEY_ESCAPE] = GLFW_RELEASE;
  frame(&application, 0.02);
  keys[GLFW_KEY_ESCAPE] = GLFW_PRESS;
  frame(&application, 0.02);
  assert(!application.paused && cursorMode == GLFW_CURSOR_DISABLED);
  assert(application.renderer.viewDistance == MIN_VIEW_DISTANCE);
  keys[GLFW_KEY_ESCAPE] = GLFW_RELEASE;
  frame(&application, 0.02);
  keys[GLFW_KEY_ESCAPE] = GLFW_PRESS;
  frame(&application, 0.02);
  click_button(&application, "SETTINGS");
  assert(application.settingsOpen && application.renderer.viewDistance == MIN_VIEW_DISTANCE);
  world_destroy(&application.world);
}

int main(void) {
  Application application = {0};
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
