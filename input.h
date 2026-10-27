#ifndef INPUT_H
#define INPUT_H

#include <GLFW/glfw3.h>
#include <stdint.h>

#include "game_logic.h"

/* Virtual actions. Bindings and state can contain several actions at once. */
typedef enum {
  INPUT_FORWARD = 1u << 0,
  INPUT_BACKWARD = 1u << 1,
  INPUT_STRAFE_LEFT = 1u << 2,
  INPUT_STRAFE_RIGHT = 1u << 3,
  INPUT_JUMP = 1u << 4,
  INPUT_RUN = 1u << 5,
  INPUT_CROUCH = 1u << 6,
  INPUT_PLACE = 1u << 7,
  INPUT_REMOVE = 1u << 8,
  INPUT_PAUSE = 1u << 9
} InputAction;

typedef struct {
  double lastX, lastY;
  double deltaX, deltaY;
  int firstMouse;
} MouseInput;

typedef struct {
  /* Indexed by GLFW key/button. Zero unbinds; multiple sources may share an action.
     Change these after input_init, e.g. keyBindings[GLFW_KEY_UP] = INPUT_FORWARD. */
  uint32_t keyBindings[GLFW_KEY_LAST + 1];
  uint32_t mouseBindings[GLFW_MOUSE_BUTTON_LAST + 1];
  uint32_t held;
  uint32_t pressed; /* Newly held actions and buffered mouse clicks; cleared by poll/discard. */
  uint32_t pendingMousePresses;
  MouseInput mouse;
} InputState;

/* Installs the existing default controls using only inline, bounded storage. */
void input_init(InputState *input);
/* Window user pointer must point to its Application before attaching callbacks. */
void input_attach(GLFWwindow *window);
/* Call once after glfwPollEvents, including while paused. */
void input_poll(InputState *input, GLFWwindow *window);
/* Discard transient gameplay input at pause/resume, retaining held actions. */
void input_discard(InputState *input);
PlayerInput input_player(const InputState *input);

#endif
