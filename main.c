#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define PI 3.14159265f

typedef struct {
  float x;
  float y;
  float z;
} Vec3;

typedef float mat4[16];

typedef struct {
  float yaw;
  float pitch;
  Vec3 front;
  Vec3 up;
  float lastX;
  float lastY;
  int firstMouse;
} Camera;

typedef struct {
  Vec3 position;
  float velocityY;
  int grounded;
  float eyeHeight;
  Camera camera;
} Player;

typedef struct {
  float forward;
  float strafe;
  int jump;
} PlayerInput;

typedef struct {
  Vec3 position;
  Vec3 size;
} Block;

typedef struct {
  GLuint vao;
  GLuint vbo;
  GLuint program;
  GLuint texture;
  GLint mvpLocation;
} Renderer;

typedef struct {
  Block *blocks;
  int blockCount;
  int blocksPerSide;
  Vec3 center;
  float blockSize;
} World;

typedef struct {
  Player *player;
  float accumulatedX;
  float accumulatedY;
} MouseState;

static const Vec3 CENTER = {0.0f, 0.0f, 0.0f};
static const float GRAVITY = 9.81f;
static const float BLOCK_SIZE = 1.0f;

static Vec3 vec3_add(Vec3 a, Vec3 b);
static Vec3 vec3_sub(Vec3 a, Vec3 b);
static Vec3 vec3_scale(Vec3 v, float scalar);
static Vec3 vec3_normalize(Vec3 v);
static Vec3 vec3_cross(Vec3 a, Vec3 b);
static float vec3_dot(Vec3 a, Vec3 b);
static void mat4_identity(mat4 dest);
static void mat4_perspective(float fov_radians, float aspect, float near, float far,
                            mat4 dest);
static void mat4_lookat(Vec3 eye, Vec3 target, Vec3 up, mat4 dest);
static void mat4_rotate(mat4 dest, float angle, float x, float y, float z);
static void mat4_multiply(mat4 a, mat4 b, mat4 dest);
static void update_camera_front(Camera *camera);
static void apply_mouse_input(Player *player, MouseState *mouseState);
static void mouse_callback(GLFWwindow *window, double xpos, double ypos);
static PlayerInput read_player_input(GLFWwindow *window);
static GLuint create_shader_program(void);
static GLuint compile_shader(GLenum type, const char *source);
static void create_cube_mesh(GLuint *vao, GLuint *vbo);
static GLuint create_grass_texture(void);
static void create_world(World *world, int blocksPerSide, Vec3 center, float blockSize);
static void destroy_world(World *world);
static void render_world(const Renderer *renderer, const World *world,
                        const Player *player, float aspect);
static void update_player(Player *updatedPlayer, PlayerInput input, float deltaTime,
                          const Block blocks[], int blockCount);

static Vec3 vec3_add(Vec3 a, Vec3 b) {
  return (Vec3){a.x + b.x, a.y + b.y, a.z + b.z};
}

static Vec3 vec3_sub(Vec3 a, Vec3 b) {
  return (Vec3){a.x - b.x, a.y - b.y, a.z - b.z};
}

static Vec3 vec3_scale(Vec3 v, float scalar) {
  return (Vec3){v.x * scalar, v.y * scalar, v.z * scalar};
}

static Vec3 vec3_normalize(Vec3 v) {
  float length = sqrtf(vec3_dot(v, v));
  if (length == 0.0f)
    return (Vec3){0.0f, 0.0f, 0.0f};
  return vec3_scale(v, 1.0f / length);
}

static Vec3 vec3_cross(Vec3 a, Vec3 b) {
  return (Vec3){a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x};
}

static float vec3_dot(Vec3 a, Vec3 b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

static void mat4_identity(mat4 dest) {
  for (int i = 0; i < 16; i++)
    dest[i] = 0.0f;
  dest[0] = dest[5] = dest[10] = dest[15] = 1.0f;
}

static void mat4_rotate(mat4 dest, float angle, float x, float y, float z) {
  float c = cosf(angle);
  float s = sinf(angle);
  float len = sqrtf(x * x + y * y + z * z);
  if (len == 0.0f) {
    mat4_identity(dest);
    return;
  }

  x /= len;
  y /= len;
  z /= len;
  float ic = 1.0f - c;

  mat4_identity(dest);
  dest[0] = x * x * ic + c;
  dest[4] = x * y * ic - z * s;
  dest[8] = x * z * ic + y * s;
  dest[1] = y * x * ic + z * s;
  dest[5] = y * y * ic + c;
  dest[9] = y * z * ic - x * s;
  dest[2] = x * z * ic - y * s;
  dest[6] = y * z * ic + x * s;
  dest[10] = z * z * ic + c;
}

static void mat4_perspective(float fov_radians, float aspect, float near, float far,
                            mat4 dest) {
  for (int i = 0; i < 16; i++)
    dest[i] = 0.0f;

  float tanHalfFovy = tanf(fov_radians / 2.0f);
  dest[0] = 1.0f / (aspect * tanHalfFovy);
  dest[5] = 1.0f / tanHalfFovy;
  dest[10] = -(far + near) / (far - near);
  dest[11] = -1.0f;
  dest[14] = -(2.0f * far * near) / (far - near);
}

static void mat4_lookat(Vec3 eye, Vec3 target, Vec3 up, mat4 dest) {
  Vec3 f = vec3_normalize(vec3_sub(target, eye));
  Vec3 s = vec3_normalize(vec3_cross(f, up));
  Vec3 u = vec3_cross(s, f);

  mat4_identity(dest);
  dest[0] = s.x;
  dest[4] = s.y;
  dest[8] = s.z;
  dest[1] = u.x;
  dest[5] = u.y;
  dest[9] = u.z;
  dest[2] = -f.x;
  dest[6] = -f.y;
  dest[10] = -f.z;

  dest[12] = -vec3_dot(s, eye);
  dest[13] = -vec3_dot(u, eye);
  dest[14] = vec3_dot(f, eye);
}

static void mat4_multiply(mat4 a, mat4 b, mat4 dest) {
  mat4 res;
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      res[j * 4 + i] =
          a[0 * 4 + i] * b[j * 4 + 0] + a[1 * 4 + i] * b[j * 4 + 1] +
          a[2 * 4 + i] * b[j * 4 + 2] + a[3 * 4 + i] * b[j * 4 + 3];
    }
  }
  for (int i = 0; i < 16; i++)
    dest[i] = res[i];
}

static void update_camera_front(Camera *camera) {
  float radYaw = camera->yaw * PI / 180.0f;
  float radPitch = camera->pitch * PI / 180.0f;
  camera->front.x = cosf(radYaw) * cosf(radPitch);
  camera->front.y = sinf(radPitch);
  camera->front.z = sinf(radYaw) * cosf(radPitch);
  camera->front = vec3_normalize(camera->front);
}

static void apply_mouse_input(Player *player, MouseState *mouseState) {
  if (player == NULL || mouseState == NULL)
    return;

  float sensitivity = 0.1f;
  player->camera.yaw += mouseState->accumulatedX * sensitivity;
  player->camera.pitch += mouseState->accumulatedY * sensitivity;

  if (player->camera.pitch > 89.0f)
    player->camera.pitch = 89.0f;
  if (player->camera.pitch < -89.0f)
    player->camera.pitch = -89.0f;

  update_camera_front(&player->camera);
  mouseState->accumulatedX = 0.0f;
  mouseState->accumulatedY = 0.0f;
}

static void mouse_callback(GLFWwindow *window, double xpos, double ypos) {
  MouseState *mouseState = glfwGetWindowUserPointer(window);
  if (mouseState == NULL || mouseState->player == NULL)
    return;

  Player *player = mouseState->player;
  if (player->camera.firstMouse) {
    player->camera.lastX = (float)xpos;
    player->camera.lastY = (float)ypos;
    player->camera.firstMouse = 0;
    return;
  }

  float xoffset = (float)xpos - player->camera.lastX;
  float yoffset = player->camera.lastY - (float)ypos;
  player->camera.lastX = (float)xpos;
  player->camera.lastY = (float)ypos;

  mouseState->accumulatedX += xoffset;
  mouseState->accumulatedY += yoffset;
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
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, textureWidth, textureHeight, 0,
               GL_RGB, GL_UNSIGNED_BYTE, pixels);

  return texture;
}

static void create_cube_mesh(GLuint *vao, GLuint *vbo) {
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
  glBindVertexArray(*vao);
  glBindBuffer(GL_ARRAY_BUFFER, *vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                        (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);
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

static void create_world(World *world, int blocksPerSide, Vec3 center,
                        float blockSize) {
  world->blocksPerSide = blocksPerSide;
  world->center = center;
  world->blockSize = blockSize;
  world->blockCount = blocksPerSide * blocksPerSide;
  world->blocks = calloc((size_t)world->blockCount, sizeof(Block));
  if (world->blocks == NULL) {
    fprintf(stderr, "Failed to allocate world blocks\n");
    exit(1);
  }

  for (int x = 0; x < blocksPerSide; x++) {
    for (int z = 0; z < blocksPerSide; z++) {
      int index = x * blocksPerSide + z;
      world->blocks[index].position =
          (Vec3){.x = center.x + ((float)x - blocksPerSide / 2.0f + 0.5f) * blockSize,
                 .y = center.y - blockSize / 2.0f,
                 .z = center.z + ((float)z - blocksPerSide / 2.0f + 0.5f) * blockSize};
      world->blocks[index].size = (Vec3){blockSize, blockSize, blockSize};
    }
  }
}

static void destroy_world(World *world) {
  if (world != NULL) {
    free(world->blocks);
    world->blocks = NULL;
    world->blockCount = 0;
  }
}

static void render_world(const Renderer *renderer, const World *world,
                        const Player *player, float aspect) {
  mat4 proj, view, model, temp, mvp;
  mat4_perspective(75.0f * PI / 180.0f, aspect, 0.1f, 100.0f, proj);
  mat4_rotate(temp, 0.0f, 0.0f, 0.0f, 0.0f);

  Vec3 cameraPosition = player->position;
  cameraPosition.y += player->eyeHeight;
  Vec3 target = vec3_add(cameraPosition, player->camera.front);
  mat4_lookat(cameraPosition, target, player->camera.up, view);

  glUseProgram(renderer->program);
  glBindVertexArray(renderer->vao);
  glBindTexture(GL_TEXTURE_2D, renderer->texture);

  for (int i = 0; i < world->blockCount; i++) {
    const Block *block = &world->blocks[i];
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

static void update_player(Player *updatedPlayer, PlayerInput input, float deltaTime,
                          const Block blocks[], int blockCount) {
  const float moveSpeed = 4.0f;
  Vec3 frontXZ = vec3_normalize(
      (Vec3){updatedPlayer->camera.front.x, 0.0f, updatedPlayer->camera.front.z});
  Vec3 right = vec3_normalize(vec3_cross(frontXZ, updatedPlayer->camera.up));
  Vec3 movement = vec3_add(vec3_scale(frontXZ, input.forward),
                           vec3_scale(right, input.strafe));
  if (vec3_dot(movement, movement) > 1.0f)
    movement = vec3_normalize(movement);

  updatedPlayer->position =
      vec3_add(updatedPlayer->position, vec3_scale(movement, moveSpeed * deltaTime));

  float previousY = updatedPlayer->position.y;
  updatedPlayer->grounded = 0;
  updatedPlayer->velocityY -= GRAVITY * deltaTime;
  updatedPlayer->position.y += updatedPlayer->velocityY * deltaTime;

  for (int i = 0; i < blockCount; i++) {
    const Block *block = &blocks[i];
    float halfWidth = block->size.x / 2.0f;
    float halfDepth = block->size.z / 2.0f;
    float blockTop = block->position.y + block->size.y / 2.0f;

    if (updatedPlayer->position.x >= block->position.x - halfWidth &&
        updatedPlayer->position.x <= block->position.x + halfWidth &&
        updatedPlayer->position.z >= block->position.z - halfDepth &&
        updatedPlayer->position.z <= block->position.z + halfDepth &&
        previousY >= blockTop && updatedPlayer->position.y <= blockTop) {
      updatedPlayer->position.y = blockTop;
      updatedPlayer->velocityY = 0.0f;
      updatedPlayer->grounded = 1;
      break;
    }
  }

  if (input.jump && updatedPlayer->grounded) {
    updatedPlayer->velocityY = 5.0f;
    updatedPlayer->grounded = 0;
  }
}

int main(void) {
  if (!glfwInit()) {
    fprintf(stderr, "Failed to initialize GLFW\n");
    return -1;
  }

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

  GLFWwindow *window = glfwCreateWindow(800, 600, "bwildering 0.1", NULL, NULL);
  if (!window) {
    fprintf(stderr, "Failed to create GLFW window\n");
    glfwTerminate();
    return -1;
  }

  glfwMakeContextCurrent(window);
  glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

  Player player = {.position = {.x = 0.0f, .y = 2.0f, .z = 2.0f},
                  .eyeHeight = 1.0f,
                  .camera = {.yaw = -90.0f,
                             .pitch = 0.0f,
                             .front = {.x = 0.0f, .y = 0.0f, .z = -1.0f},
                             .up = {.x = 0.0f, .y = 1.0f, .z = 0.0f},
                             .lastX = 400.0f,
                             .lastY = 300.0f,
                             .firstMouse = 1}};

  MouseState mouseState = {.player = &player, .accumulatedX = 0.0f, .accumulatedY = 0.0f};
  glfwSetWindowUserPointer(window, &mouseState);
  glfwSetCursorPosCallback(window, mouse_callback);

  glewExperimental = GL_TRUE;
  GLenum err = glewInit();
  if (err != GLEW_OK) {
    fprintf(stderr, "Failed to initialize GLEW: %s\n", glewGetErrorString(err));
    glfwDestroyWindow(window);
    glfwTerminate();
    return -1;
  }

  glEnable(GL_DEPTH_TEST);

  Renderer renderer = {0};
  renderer.program = create_shader_program();
  if (renderer.program == 0) {
    glfwDestroyWindow(window);
    glfwTerminate();
    return -1;
  }

  create_cube_mesh(&renderer.vao, &renderer.vbo);
  renderer.mvpLocation = glGetUniformLocation(renderer.program, "mvp");
  renderer.texture = create_grass_texture();

  World world = {0};
  create_world(&world, 32, CENTER, BLOCK_SIZE);

  float lastFrame = (float)glfwGetTime();

  while (!glfwWindowShouldClose(window)) {
    float currentFrame = (float)glfwGetTime();
    float deltaTime = currentFrame - lastFrame;
    lastFrame = currentFrame;

    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
      glfwSetWindowShouldClose(window, GLFW_TRUE);

    apply_mouse_input(&player, &mouseState);

    PlayerInput input = read_player_input(window);
    update_player(&player, input, deltaTime, world.blocks, world.blockCount);

    glClearColor(0.15f, 0.15f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    if (height == 0)
      height = 1;

    render_world(&renderer, &world, &player, (float)width / (float)height);

    glfwSwapBuffers(window);
    glfwPollEvents();
  }

  glDeleteTextures(1, &renderer.texture);
  glDeleteVertexArrays(1, &renderer.vao);
  glDeleteBuffers(1, &renderer.vbo);
  glDeleteProgram(renderer.program);
  destroy_world(&world);
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
