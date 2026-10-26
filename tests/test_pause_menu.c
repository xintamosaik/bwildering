/* Drive the real application update and mouse callbacks without an X11/GL context. */
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <assert.h>
#include <string.h>

static int keys[GLFW_KEY_LAST + 1];
static int cursorMode = GLFW_CURSOR_DISABLED;
static double now, cursorX, cursorY;
static int closeRequested, pendingClick;
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
  (void)window; *width = 640; *height = 480;
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
#define glfwGetFramebufferSize test_get_size
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

int main(void) {
  Application application = {0};
  windowUser = &application;
  assert(create_platform(&application.world, 16, (Vec3){0}, 1, 1));
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
  assert(memcmp(&application.player, &frozen, sizeof(frozen)) == 0);
  keys[GLFW_KEY_ESCAPE] = GLFW_RELEASE;
  keys[GLFW_KEY_SPACE] = GLFW_PRESS;
  mouse_callback(application.window, 100, 100);
  mouse_callback(application.window, 200, 150);
  pendingClick = GLFW_MOUSE_BUTTON_RIGHT + 1;
  frame(&application, 10);
  assert(application.world.voxelCount == voxels);
  assert(memcmp(&application.player, &frozen, sizeof(frozen)) == 0);
  /* Click outside the buttons: remain paused and do not place anything. */
  cursorX = 10; cursorY = 10;
  pendingClick = GLFW_MOUSE_BUTTON_LEFT + 1;
  frame(&application, 0.02);
  assert(application.paused && application.world.voxelCount == voxels);

  /* Resume with the mouse. The resume click and elapsed pause cannot affect gameplay. */
  cursorX = 320; cursorY = 240;
  pendingClick = GLFW_MOUSE_BUTTON_LEFT + 1;
  frame(&application, 20);
  assert(!application.paused && cursorMode == GLFW_CURSOR_DISABLED);
  assert(memcmp(&application.player, &frozen, sizeof(frozen)) == 0);
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
  assert(memcmp(&application.player, &frozen, sizeof(frozen)) == 0);

  keys[GLFW_KEY_ESCAPE] = GLFW_RELEASE;
  frame(&application, 0.02);
  keys[GLFW_KEY_ESCAPE] = GLFW_PRESS;
  frame(&application, 0.02);
  assert(application.paused);
  cursorX = 320; cursorY = 304;
  pendingClick = GLFW_MOUSE_BUTTON_LEFT + 1;
  frame(&application, 0.02);
  assert(closeRequested);
  world_destroy(&application.world);
  puts("All pause menu interaction tests passed.");
  return 0;
}
