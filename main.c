#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "game_logic.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#pragma region Types & Constants
typedef struct {
  double lastX;
  double lastY;
  double deltaX;
  double deltaY;
  int firstMouse;
} MouseInput;

typedef struct {
  GLuint vao;
  GLuint vbo;
  GLuint program;
  GLuint texture;
  GLint mvpLocation;
  int framebufferWidth;
  int framebufferHeight;
} Renderer;

typedef struct {
  GLFWwindow *window;
  Player player;
  MouseInput mouseInput;
  Renderer renderer;
  World world;
  double lastFrame;
  double simulationAccumulator;
  int jumpWasDown;
  int jumpQueued;
  int escapeWasDown;
  int glfwInitialized;
  int rendererInitialized;
} Application;

/** World-space origin used to center the starter platform. */
static const Vec3 CENTER = {0.0f, 0.0f, 0.0f};
/** Width, height, and depth of each default platform block, in world units. */
static const float BLOCK_SIZE = 1.0f;
static const double FIXED_DELTA_TIME = 1.0 / 60.0;
static const double MAX_FRAME_DELTA_TIME = 0.25;

#pragma endregion

#pragma region Input
static void mouse_callback(GLFWwindow *window, double xpos, double ypos) {
  if (glfwGetInputMode(window, GLFW_CURSOR) != GLFW_CURSOR_DISABLED)
    return;
  Application *application = glfwGetWindowUserPointer(window);
  if (application == NULL)
    return;
  MouseInput *mouseInput = &application->mouseInput;
  if (mouseInput->firstMouse) {
    mouseInput->lastX = xpos;
    mouseInput->lastY = ypos;
    mouseInput->firstMouse = 0;
    return;
  }

  mouseInput->deltaX += xpos - mouseInput->lastX;
  mouseInput->deltaY += mouseInput->lastY - ypos;
  mouseInput->lastX = xpos;
  mouseInput->lastY = ypos;
}

static PlayerInput read_player_input(GLFWwindow *window) {
  PlayerInput input = {0};
  input.forward = (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) -
                  (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS);
  input.strafe = (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) -
                 (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS);
  input.jump = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
  return input;
}

#pragma endregion

#pragma region Camera
static void update_camera_front(Camera *camera) {
  float radYaw = camera->yaw * 3.14159265f / 180.0f;
  float radPitch = camera->pitch * 3.14159265f / 180.0f;

  camera->front.x = cosf(radYaw) * cosf(radPitch);
  camera->front.y = sinf(radPitch);
  camera->front.z = sinf(radYaw) * cosf(radPitch);
  camera->front = vec3_normalize(camera->front);
}

static void update_camera(Camera *camera, MouseInput *input) {
  const float sensitivity = 0.1f;
  camera->yaw += (float)input->deltaX * sensitivity;
  camera->pitch += (float)input->deltaY * sensitivity;
  input->deltaX = 0.0;
  input->deltaY = 0.0;

  if (camera->pitch > 89.0f)
    camera->pitch = 89.0f;
  if (camera->pitch < -89.0f)
    camera->pitch = -89.0f;

  update_camera_front(camera);
}

#pragma endregion

#pragma region Rendering
static GLuint create_grass_texture(void) {
  const int textureWidth = 16;
  const int textureHeight = 16;
  unsigned char pixels[16 * 16 * 3];

  for (int y = 0; y < textureHeight; y++) {
    for (int x = 0; x < textureWidth; x++) {
      int check = ((x / 4) + (y / 4)) % 2;
      int index = (y * textureWidth + x) * 3;
      pixels[index + 0] = check ? 34 : 58;
      pixels[index + 1] = check ? 139 : 180;
      pixels[index + 2] = check ? 34 : 58;
    }
  }

  GLuint texture;
  glGenTextures(1, &texture);
  if (texture == 0)
    return 0;
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, textureWidth, textureHeight, 0,
               GL_RGB, GL_UNSIGNED_BYTE, pixels);

  return texture;
}

/** Requires a current OpenGL context and valid output pointers. */
static int create_cube_mesh(GLuint *vao, GLuint *vbo) {
  const float vertices[] = {
      -0.5f, -0.5f, -0.5f, 0.0f, 0.0f, 0.5f, -0.5f, -0.5f, 1.0f, 0.0f, 0.5f,
      0.5f, -0.5f, 1.0f, 1.0f, 0.5f, 0.5f, -0.5f, 1.0f, 1.0f, -0.5f, 0.5f,
      -0.5f, 0.0f, 1.0f, -0.5f, -0.5f, -0.5f, 0.0f, 0.0f,

      -0.5f, -0.5f, 0.5f, 0.0f, 0.0f, 0.5f, -0.5f, 0.5f, 1.0f, 0.0f, 0.5f,
      0.5f, 0.5f, 1.0f, 1.0f, 0.5f, 0.5f, 0.5f, 1.0f, 1.0f, -0.5f, 0.5f,
      0.5f, 0.0f, 1.0f, -0.5f, -0.5f, 0.5f, 0.0f, 0.0f,

      -0.5f, 0.5f, 0.5f, 1.0f, 0.0f, -0.5f, 0.5f, -0.5f, 1.0f, 1.0f, -0.5f,
      -0.5f, -0.5f, 0.0f, 1.0f, -0.5f, -0.5f, -0.5f, 0.0f, 1.0f, -0.5f,
      -0.5f, 0.5f, 0.0f, 0.0f, -0.5f, 0.5f, 0.5f, 1.0f, 0.0f,

      0.5f, 0.5f, 0.5f, 1.0f, 0.0f, 0.5f, 0.5f, -0.5f, 1.0f, 1.0f, 0.5f,
      -0.5f, -0.5f, 0.0f, 1.0f, 0.5f, -0.5f, -0.5f, 0.0f, 1.0f, 0.5f,
      -0.5f, 0.5f, 0.0f, 0.0f, 0.5f, 0.5f, 0.5f, 1.0f, 0.0f,

      -0.5f, -0.5f, -0.5f, 0.0f, 1.0f, 0.5f, -0.5f, -0.5f, 1.0f, 1.0f, 0.5f,
      -0.5f, 0.5f, 1.0f, 0.0f, 0.5f, -0.5f, 0.5f, 1.0f, 0.0f, -0.5f, -0.5f,
      0.5f, 0.0f, 0.0f, -0.5f, -0.5f, -0.5f, 0.0f, 1.0f,

      -0.5f, 0.5f, -0.5f, 0.0f, 1.0f, 0.5f, 0.5f, -0.5f, 1.0f, 1.0f, 0.5f,
      0.5f, 0.5f, 1.0f, 0.0f, 0.5f, 0.5f, 0.5f, 1.0f, 0.0f, -0.5f, 0.5f,
      0.5f, 0.0f, 0.0f, -0.5f, 0.5f, -0.5f, 0.0f, 1.0f};

  glGenVertexArrays(1, vao);
  glGenBuffers(1, vbo);
  if (*vao == 0 || *vbo == 0)
    return 0;
  glBindVertexArray(*vao);
  glBindBuffer(GL_ARRAY_BUFFER, *vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                        (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);
  return 1;
}

static GLuint compile_shader(GLenum type, const char *source) {
  GLuint shader = glCreateShader(type);
  if (shader == 0) {
    fprintf(stderr, "Failed to create shader\n");
    return 0;
  }

  glShaderSource(shader, 1, &source, NULL);
  glCompileShader(shader);

  GLint success;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
  if (!success) {
    char log[512];
    glGetShaderInfoLog(shader, sizeof(log), NULL, log);
    fprintf(stderr, "Shader compilation failed: %s\n", log);
    glDeleteShader(shader);
    return 0;
  }

  return shader;
}

static GLuint create_shader_program(void) {
  const char *vertexShaderSource =
      "#version 330 core\n"
      "layout (location = 0) in vec3 aPos;\n"
      "layout (location = 1) in vec2 aTexCoord;\n"
      "out vec2 TexCoord;\n"
      "uniform mat4 mvp;\n"
      "void main() {\n"
      "    gl_Position = mvp * vec4(aPos, 1.0);\n"
      "    TexCoord = aTexCoord;\n"
      "}\n";

  const char *fragmentShaderSource =
      "#version 330 core\n"
      "in vec2 TexCoord;\n"
      "out vec4 FragColor;\n"
      "uniform sampler2D ourTexture;\n"
      "void main() {\n"
      "    FragColor = texture(ourTexture, TexCoord);\n"
      "}\n";

  GLuint vertexShader = compile_shader(GL_VERTEX_SHADER, vertexShaderSource);
  if (vertexShader == 0)
    return 0;

  GLuint fragmentShader = compile_shader(GL_FRAGMENT_SHADER, fragmentShaderSource);
  if (fragmentShader == 0) {
    glDeleteShader(vertexShader);
    return 0;
  }

  GLuint program = glCreateProgram();
  if (program == 0) {
    fprintf(stderr, "Failed to create shader program\n");
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    return 0;
  }

  glAttachShader(program, vertexShader);
  glAttachShader(program, fragmentShader);
  glLinkProgram(program);
  glDeleteShader(vertexShader);
  glDeleteShader(fragmentShader);

  GLint success;
  glGetProgramiv(program, GL_LINK_STATUS, &success);
  if (!success) {
    char log[512];
    glGetProgramInfoLog(program, sizeof(log), NULL, log);
    fprintf(stderr, "Shader program linking failed: %s\n", log);
    glDeleteProgram(program);
    return 0;
  }

  return program;
}

static void renderer_destroy(Renderer *renderer) {
  if (renderer->vao != 0)
    glDeleteVertexArrays(1, &renderer->vao);
  if (renderer->vbo != 0)
    glDeleteBuffers(1, &renderer->vbo);
  if (renderer->texture != 0)
    glDeleteTextures(1, &renderer->texture);
  if (renderer->program != 0)
    glDeleteProgram(renderer->program);
  *renderer = (Renderer){0};
}

static int renderer_init(Renderer *renderer) {
  *renderer = (Renderer){0};
  renderer->program = create_shader_program();
  if (renderer->program == 0)
    goto fail;

  if (!create_cube_mesh(&renderer->vao, &renderer->vbo))
    goto fail;

  renderer->mvpLocation = glGetUniformLocation(renderer->program, "mvp");
  if (renderer->mvpLocation < 0) {
    fprintf(stderr, "Failed to locate mvp uniform\n");
    goto fail;
  }

  renderer->texture = create_grass_texture();
  if (renderer->texture == 0)
    goto fail;

  return 1;

fail:
  renderer_destroy(renderer);
  return 0;
}

static void render_world(const Renderer *renderer, const World *world,
                         const Player *player) {
  if (renderer->framebufferWidth <= 0 || renderer->framebufferHeight <= 0)
    return;

  glUseProgram(renderer->program);

  mat4 proj, view, mvp;
  float aspectRatio = (float)renderer->framebufferWidth /
                      (float)renderer->framebufferHeight;
  mat4_perspective(75.0f * (3.14159f / 180.0f), aspectRatio, 0.1f, 100.0f,
                   proj);
  Vec3 cameraPosition = player->position;
  cameraPosition.y += player->eyeHeight;
  Vec3 target = vec3_add(cameraPosition, player->camera.front);
  mat4_lookat(cameraPosition, target, player->camera.up, view);

  glBindVertexArray(renderer->vao);
  glBindTexture(GL_TEXTURE_2D, renderer->texture);
  for (int i = 0; i < world->blockCount; i++) {
    const Block *block = &world->blocks[i];
    mat4 model, temp;
    mat4_identity(model);
    model[0] = block->size.x;
    model[5] = block->size.y;
    model[10] = block->size.z;
    model[12] = block->position.x;
    model[13] = block->position.y;
    model[14] = block->position.z;

    mat4_multiply(view, model, temp);
    mat4_multiply(proj, temp, mvp);

    glUniformMatrix4fv(renderer->mvpLocation, 1, GL_FALSE, (float *)mvp);
    glDrawArrays(GL_TRIANGLES, 0, 36);
  }
}

#pragma endregion

#pragma region Application
static void framebuffer_size_callback(GLFWwindow *window, int width,
                                      int height) {
  Application *application = glfwGetWindowUserPointer(window);
  if (application == NULL)
    return;
  application->renderer.framebufferWidth = width;
  application->renderer.framebufferHeight = height;
  glViewport(0, 0, width, height);
}

static void application_shutdown(Application *application) {
  if (application->rendererInitialized) {
    renderer_destroy(&application->renderer);
    application->rendererInitialized = 0;
  }
  if (application->window != NULL) {
    glfwDestroyWindow(application->window);
    application->window = NULL;
  }
  if (application->glfwInitialized) {
    glfwTerminate();
    application->glfwInitialized = 0;
  }
}

static int application_init(Application *application) {
  *application = (Application){
      .player = {.position = {.x = 0.0f, .y = 2.0f, .z = 2.0f},
                 .eyeHeight = 1.0f,
                 .camera = {.yaw = -90.0f,
                            .pitch = 0.0f,
                            .front = {.x = 0.0f, .y = 0.0f, .z = -1.0f},
                            .up = {.x = 0.0f, .y = 1.0f, .z = 0.0f}}},
      .mouseInput = {.firstMouse = 1},
  };

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

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

  application->window =
      glfwCreateWindow(800, 600, "bwildering 0.1", NULL, NULL);
  if (application->window == NULL) {
    fprintf(stderr, "Failed to create GLFW window\n");
    goto fail;
  }
  glfwMakeContextCurrent(application->window);

  glfwSetWindowUserPointer(application->window, application);
  glfwSetCursorPosCallback(application->window, mouse_callback);
  glfwSetInputMode(application->window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

  glewExperimental = GL_TRUE;
  GLenum glewStatus = glewInit();
  if (glewStatus != GLEW_OK) {
    fprintf(stderr, "Failed to initialize GLEW: %s\n",
            glewGetErrorString(glewStatus));
    goto fail;
  }

  glEnable(GL_DEPTH_TEST);

  if (!renderer_init(&application->renderer))
    goto fail;
  application->rendererInitialized = 1;

  glfwGetFramebufferSize(application->window,
                          &application->renderer.framebufferWidth,
                          &application->renderer.framebufferHeight);
  glViewport(0, 0, application->renderer.framebufferWidth,
             application->renderer.framebufferHeight);
  glfwSetFramebufferSizeCallback(application->window,
                                 framebuffer_size_callback);

  if (!create_platform(&application->world, PLATFORM_BLOCKS_PER_SIDE, CENTER,
                       BLOCK_SIZE)) {
    fprintf(stderr, "Failed to create platform: world capacity is %d blocks\n",
            WORLD_MAX_BLOCKS);
    goto fail;
  }
  application->lastFrame = glfwGetTime();
  return 1;

fail:
  application_shutdown(application);
  return 0;
}

static void application_update(Application *application) {
  GLFWwindow *window = application->window;
  glfwPollEvents();

  double currentFrame = glfwGetTime();
  double deltaTime = currentFrame - application->lastFrame;
  application->lastFrame = currentFrame;
  if (deltaTime < 0.0)
    deltaTime = 0.0;
  if (deltaTime > MAX_FRAME_DELTA_TIME)
    deltaTime = MAX_FRAME_DELTA_TIME;

  int escapeIsDown = glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
  if (escapeIsDown && !application->escapeWasDown) {
    int cursorCaptured =
        glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED;
    glfwSetInputMode(window, GLFW_CURSOR,
                     cursorCaptured ? GLFW_CURSOR_NORMAL
                                    : GLFW_CURSOR_DISABLED);
    application->mouseInput.firstMouse = 1;
    application->mouseInput.deltaX = 0.0;
    application->mouseInput.deltaY = 0.0;
  }
  application->escapeWasDown = escapeIsDown;

  update_camera(&application->player.camera, &application->mouseInput);
  PlayerInput input = read_player_input(window);
  if (input.jump && !application->jumpWasDown)
    application->jumpQueued = 1;
  application->jumpWasDown = input.jump;
  input.jump = 0;

  application->simulationAccumulator += deltaTime;
  while (application->simulationAccumulator >= FIXED_DELTA_TIME) {
    input.jump = application->jumpQueued;
    update_player(&application->player, input, (float)FIXED_DELTA_TIME,
                  &application->world);
    application->jumpQueued = 0;
    application->simulationAccumulator -= FIXED_DELTA_TIME;
  }
}

static void application_render(const Application *application) {
  glClearColor(0.15f, 0.15f, 0.2f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  render_world(&application->renderer, &application->world,
               &application->player);
  glfwSwapBuffers(application->window);
}

#pragma endregion

#pragma region Main
int main(void) {
  Application application;
  if (!application_init(&application))
    return EXIT_FAILURE;

  while (!glfwWindowShouldClose(application.window)) {
    application_update(&application);
    application_render(&application);
  }

  application_shutdown(&application);
  return EXIT_SUCCESS;
}
#pragma endregion
