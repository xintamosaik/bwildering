#include "input.h"

void input_init(InputState *const input) {
  *input = (InputState){
      .keyBindings = {
          [GLFW_KEY_W] = INPUT_FORWARD,
          [GLFW_KEY_S] = INPUT_BACKWARD,
          [GLFW_KEY_A] = INPUT_STRAFE_LEFT,
          [GLFW_KEY_D] = INPUT_STRAFE_RIGHT,
          [GLFW_KEY_SPACE] = INPUT_JUMP,
          [GLFW_KEY_LEFT_CONTROL] = INPUT_RUN,
          [GLFW_KEY_RIGHT_CONTROL] = INPUT_RUN,
          [GLFW_KEY_LEFT_SHIFT] = INPUT_CROUCH,
          [GLFW_KEY_RIGHT_SHIFT] = INPUT_CROUCH,
          [GLFW_KEY_ESCAPE] = INPUT_PAUSE,
      },
      .mouseBindings = {
          [GLFW_MOUSE_BUTTON_LEFT] = INPUT_PLACE,
          [GLFW_MOUSE_BUTTON_RIGHT] = INPUT_REMOVE,
      },
      .mouse = {.firstMouse = 1},
  };
}

static void mouse_button_callback(GLFWwindow *const window, const int button,
                                  const int action, const int mods) {
  (void)mods;
  if (action != GLFW_PRESS)
    return;
  InputState *const input = glfwGetWindowUserPointer(window);
  if (!input || button < 0 || button > GLFW_MOUSE_BUTTON_LAST)
    return;
  /* Preserve clicks pressed and released between polls, including their position. */
  input->pendingMousePresses |= input->mouseBindings[button];
  if (button == GLFW_MOUSE_BUTTON_LEFT && input->clickCount < INPUT_CLICK_CAPACITY) {
    InputClick *const click = &input->clicks[input->clickCount++];
    glfwGetCursorPos(window, &click->x, &click->y);
  }
}

static void mouse_callback(GLFWwindow *const window, const double x, const double y) {
  if (glfwGetInputMode(window, GLFW_CURSOR) != GLFW_CURSOR_DISABLED)
    return;
  InputState *const input = glfwGetWindowUserPointer(window);
  if (!input)
    return;
  MouseInput *const mouse = &input->mouse;
  if (!mouse->firstMouse) {
    mouse->deltaX += x - mouse->lastX;
    mouse->deltaY += mouse->lastY - y;
  }
  mouse->firstMouse = 0;
  mouse->lastX = x;
  mouse->lastY = y;
}

static void key_callback(GLFWwindow *const window, const int key, const int scancode,
                         const int action, const int mods) {
  (void)scancode;
  (void)mods;
  InputState *const input = glfwGetWindowUserPointer(window);
  if (input && action == GLFW_PRESS && key >= GLFW_KEY_SPACE && key <= GLFW_KEY_LAST)
    input->pendingKeyPresses |= input->keyBindings[key] & ~input->held;
}

static void framebuffer_size_callback(GLFWwindow *const window, const int width,
                                      const int height) {
  InputState *const input = glfwGetWindowUserPointer(window);
  if (!input)
    return;
  input->framebufferWidth = width;
  input->framebufferHeight = height;
  input->framebufferResized = 1;
}

void input_attach(InputState *const input, GLFWwindow *const window) {
  glfwSetWindowUserPointer(window, input);
  glfwSetKeyCallback(window, key_callback);
  glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
  glfwSetCursorPosCallback(window, mouse_callback);
  glfwSetMouseButtonCallback(window, mouse_button_callback);
}

void input_poll(InputState *const input, GLFWwindow *const window) {
  uint32_t held = 0;
  /* GLFW_KEY_UNKNOWN is -1; the first valid key is GLFW_KEY_SPACE. */
  for (int key = GLFW_KEY_SPACE; key <= GLFW_KEY_LAST; key++)
    if (input->keyBindings[key] && glfwGetKey(window, key) == GLFW_PRESS)
      held |= input->keyBindings[key];
  for (int button = 0; button <= GLFW_MOUSE_BUTTON_LAST; button++)
    if (input->mouseBindings[button] && glfwGetMouseButton(window, button) == GLFW_PRESS)
      held |= input->mouseBindings[button];
  input->pressed = (held & ~input->held) | input->pendingMousePresses | input->pendingKeyPresses;
  input->held = held;
  input->pendingMousePresses = 0;
  input->pendingKeyPresses = 0;
}

void input_discard(InputState *const input) {
  input->clickCount = 0;
  input->pressed = 0;
  input->pendingMousePresses = 0;
  input->pendingKeyPresses = 0;
  input->mouse.firstMouse = 1;
  input->mouse.deltaX = 0.0;
  input->mouse.deltaY = 0.0;
}

PlayerInput input_player(const InputState *const input) {
  const uint32_t held = input->held;
  return (PlayerInput){
      .forward = (float)(((held & INPUT_FORWARD) != 0) - ((held & INPUT_BACKWARD) != 0)),
      .strafe = (float)(((held & INPUT_STRAFE_RIGHT) != 0) - ((held & INPUT_STRAFE_LEFT) != 0)),
      .jump = (input->pressed & INPUT_JUMP) != 0,
      .swimUp = (held & INPUT_JUMP) != 0,
      .run = (held & INPUT_RUN) != 0,
      .crouch = (held & INPUT_CROUCH) != 0,
  };
}
