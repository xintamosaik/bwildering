#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
  float x;
  float y;
  float z;
} Vec3;

Vec3 vec3_add(Vec3 a, Vec3 b);
Vec3 vec3_sub(Vec3 a, Vec3 b);
Vec3 vec3_scale(Vec3 v, float scalar);
Vec3 vec3_normalize(Vec3 v);
Vec3 vec3_cross(Vec3 a, Vec3 b);
float vec3_dot(Vec3 a, Vec3 b);

typedef struct {
  float yaw;
  float pitch;
  Vec3 front;
  Vec3 up;
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
  double lastX;
  double lastY;
  double deltaX;
  double deltaY;
  int firstMouse;
} MouseInput;

typedef struct {
  Vec3 position;
  Vec3 size;
} Block;

/** World-space origin used to center the starter platform. */
const Vec3 CENTER = {0.0f, 0.0f, 0.0f};
/** Downward acceleration in world units per second squared. */
const float GRAVITY = 9.81f;
/** Width, height, and depth of each default platform block, in world units. */
const float BLOCK_SIZE = 1.0f;

/** Shared player state updated by the input callback and game loop. */
Player player = {.position = {.x = 0.0f, .y = 2.0f, .z = 2.0f},
                 .eyeHeight = 1.0f,
                 .camera = {.yaw = -90.0f,
                            .pitch = 0.0f,
                            .front = {.x = 0.0f, .y = 0.0f, .z = -1.0f},
                            .up = {.x = 0.0f, .y = 1.0f, .z = 0.0f}}};

/** Cursor events accumulate here until the game loop consumes them. */
MouseInput mouseInput = {.firstMouse = 1};

/**
 * These helpers define the game's basic geometry vocabulary and matrix
 * conventions. Vectors are represented by Vec3, while matrices are flat
 * 16-element arrays in column-major order to match OpenGL. Camera and rendering
 * code use these operations to build transforms without repeating component-wise
 * arithmetic.
 */
#pragma region Vector and matrix
typedef float mat4[16];
// --- Forward Declarations ---
void mat4_identity(mat4 dest);
void mat4_perspective(float fov_radians, float aspect, float near, float far,
                      mat4 dest);
void mat4_lookat(Vec3 eye, Vec3 target, Vec3 up, mat4 dest);
void mat4_rotate(mat4 dest, float angle, float x, float y, float z);
void mat4_multiply(mat4 a, mat4 b, mat4 dest);

/** Returns the component-wise sum of two vectors. */
Vec3 vec3_add(Vec3 a, Vec3 b) {
  return (Vec3){a.x + b.x, a.y + b.y, a.z + b.z};
}

/** Returns the component-wise difference a - b. */
Vec3 vec3_sub(Vec3 a, Vec3 b) {
  return (Vec3){a.x - b.x, a.y - b.y, a.z - b.z};
}

/** Returns v multiplied by scalar. */
Vec3 vec3_scale(Vec3 v, float scalar) {
  return (Vec3){v.x * scalar, v.y * scalar, v.z * scalar};
}

/** Returns v with unit length, or the zero vector when v has zero length. */
Vec3 vec3_normalize(Vec3 v) {
  float length = sqrtf(vec3_dot(v, v));
  if (length == 0.0f)
    return (Vec3){0.0f, 0.0f, 0.0f};
  return vec3_scale(v, 1.0f / length);
}

/** Returns the right-handed cross product a x b. */
Vec3 vec3_cross(Vec3 a, Vec3 b) {
  return (Vec3){a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x};
}

/** Returns the dot product of a and b. */
float vec3_dot(Vec3 a, Vec3 b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}
/**
 * Builds a column-major rotation matrix for angle radians around an axis.
 * The axis must be nonzero; the matrix is written to dest.
 */
void mat4_rotate(mat4 dest, float angle, float x, float y, float z) {
  float c = cosf(angle);
  float s = sinf(angle);
  float len = sqrtf(x * x + y * y + z * z);
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

/** Writes the 4x4 identity matrix to dest in column-major order. */
void mat4_identity(mat4 dest) {
  for (int i = 0; i < 16; i++)
    dest[i] = 0.0f;
  dest[0] = dest[5] = dest[10] = dest[15] = 1.0f;
}

/**
 * Writes a perspective projection matrix to dest.
 * Requires 0 < fov_radians < pi, aspect > 0, and 0 < near < far.
 */
void mat4_perspective(float fov_radians, float aspect, float near, float far,
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

/**
 * Builds a column-major view matrix looking from eye toward target.
 * eye and target must differ, and up must not be parallel to the view direction.
 */
void mat4_lookat(Vec3 eye, Vec3 target, Vec3 up, mat4 dest) {
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

/** Writes the column-major product a * b to dest; dest may alias either input. */
void mat4_multiply(mat4 a, mat4 b, mat4 dest) {
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

#pragma endregion


/**
 * The camera stores its orientation as yaw and pitch angles, plus a normalized
 * forward direction used by both movement and view construction. Mouse input
 * adjusts those angles, clamps pitch to avoid looking straight up or down, and
 * then refreshes the forward vector so the rest of the frame can use one
 * consistent orientation.
 */
#pragma region Camera
/** Recomputes camera->front from its yaw and pitch, which are measured in degrees. */
void update_camera_front(Camera *camera) {
  float radYaw = camera->yaw * 3.14159265f / 180.0f;
  float radPitch = camera->pitch * 3.14159265f / 180.0f;

  camera->front.x = cosf(radYaw) * cosf(radPitch);
  camera->front.y = sinf(radPitch);
  camera->front.z = sinf(radYaw) * cosf(radPitch);
  camera->front = vec3_normalize(camera->front);
}

/** Stores cursor motion for the next frame without changing camera state. */
void mouse_callback(GLFWwindow *window, double xpos, double ypos) {
  (void)window;
  if (mouseInput.firstMouse) {
    mouseInput.lastX = xpos;
    mouseInput.lastY = ypos;
    mouseInput.firstMouse = 0;
    return;
  }

  mouseInput.deltaX += xpos - mouseInput.lastX;
  mouseInput.deltaY += mouseInput.lastY - ypos;
  mouseInput.lastX = xpos;
  mouseInput.lastY = ypos;
}

/** Applies accumulated mouse motion to the camera and then clears the deltas. */
void update_camera(Camera *camera, MouseInput *input) {
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

/** Converts the current WASD and jump key states into player movement intent. */
PlayerInput read_player_input(GLFWwindow *window) {
  PlayerInput input = {0};
  input.forward = (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) -
                  (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS);
  input.strafe = (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) -
                 (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS);
  input.jump = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
  return input;
}

#pragma endregion



/**
 * Creates and uploads the procedural 16x16 grass checker texture.
 * Requires a current OpenGL context; returns the generated texture handle.
 */
GLuint create_grass_texture(void) {
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

/**
 * Creates the cube VAO/VBO and configures position and UV attributes.
 * Requires a current OpenGL context and valid output pointers; leaves the VAO bound.
 */
void create_cube_mesh(GLuint *vao, GLuint *vbo) {
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

/** Compiles one shader stage; returns 0 and prints the compiler log on failure. */
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

/**
 * Compiles and links the application's textured-cube shaders.
 * Requires a current OpenGL context; returns 0 and logs details on failure.
 */
GLuint create_shader_program(void) {
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

/**
 * Fills a caller-provided array with a centered, flat grid of cubic blocks.
 * The array must hold blocksPerSide squared entries; returns the number written.
 */
int create_platform(Block blocks[], int blocksPerSide, Vec3 center,
                    float blockSize) {
  int blockCount = 0;
  for (int x = 0; x < blocksPerSide; x++) {
    for (int z = 0; z < blocksPerSide; z++) {
      blocks[blockCount++] = (Block){
          .position =
              {
                     .x = center.x +
                       ((float)x - blocksPerSide / 2.0f + 0.5f) * blockSize,
                  .y = center.y - blockSize / 2.0f,
                     .z = center.z +
                       ((float)z - blocksPerSide / 2.0f + 0.5f) * blockSize,
              },
          .size = {blockSize, blockSize, blockSize},
      };
    }
  }
  return blockCount;
}

/**
 * Applies camera-relative movement, gravity, block-top landing, and jumping.
 * Diagonal input is normalized to keep its speed equal to cardinal movement.
 */
void update_player(Player *updatedPlayer, PlayerInput input, float deltaTime,
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

/**



                                                              
  ▄▒ ▄▄▄▄       ▄▄▄▄                             ▄▒ ▄▄▄▄      
▄▀▓▄▀▀   ▀░▄  ▄▀▀   ▀░▄         ▄▄▄▄        ▄▒ ▄▀▓▄▀▀   ▀░▄   
 ░░▌      ▐░░░       ▐▓▓     ▄░▀   ▀▀▄██▄ ▄▒█   ░░▌      ▐░░  
 ░         ░█▌        █▓▌   ░░▌      ▐░█   ░░   ░         ░█▌ 
  ▓        ██▌        ░▓▌  ▐░█        ██   ░▓    ▓        ██▌ 
 ▓▓        ▒█▌        ░ ▌  ▐██        ██   ▓▓   ▓▓        ▒█▌ 
 ▒█        ▓▓▌        ▓▓▌  ▐█▓        █▓   ▀▓   ▒█        ▓▓▌ 
 ▓░        █▓▌        ░▄▌  ▐█▓        █▓   ▒▓   ▓░        █▓▌ 
▄█░       ▄▓▓▌       ▄░░▌   ▓▓▌      ▐██  ▄██  ▄█░       ▄▓▓▌ 
 ▀▄        ▀▒▌        ▀▄▌    ▀▒▄   ▄▄▀██▄  ▀▒   ▀▄        ▀▒▌ 
   ▀░        ▀░         ▀░      ▀▀▀▀         ▀    ▀░        ▀░

(main)

*/

/** Initializes the window and OpenGL resources, runs the game loop, then cleans up. */
int main(void) {

  // 1. Initialize GLFW
  if (!glfwInit()) {
    fprintf(stderr, "Failed to initialize GLFW\n");
    return -1;
  }

  // Configure OpenGL version 3.3 Core Profile
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

  // 2. Create Window
  GLFWwindow *window = glfwCreateWindow(800, 600, "bwildering 0.1", NULL, NULL);
  if (!window) {
    fprintf(stderr, "Failed to create GLFW window\n");
    glfwTerminate();
    return -1;
  }
  glfwMakeContextCurrent(window);

  // Capture the mouse cursor like a game
  glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
  glfwSetCursorPosCallback(window, mouse_callback);

  glewExperimental = GL_TRUE;
  glewInit();

  glEnable(GL_DEPTH_TEST);

  GLuint program = create_shader_program();
  if (program == 0) {
    glfwDestroyWindow(window);
    glfwTerminate();
    return -1;
  }
 
/**
 * The cube mesh is shared by every world block, so its vertex positions and UVs
 * are uploaded once and reused for each draw. This setup creates the VAO/VBO
 * attribute layout expected by the shaders; the MVP uniform location is kept
 * alongside it because the render loop uses that program-specific handle.
 */
#pragma region Cube mesh shader
  GLuint VAO, VBO;
  create_cube_mesh(&VAO, &VBO);
  GLint mvpLoc = glGetUniformLocation(program, "mvp");
#pragma endregion

 
  GLuint texture = create_grass_texture();

  const int PLATFORM_BLOCKS_PER_SIDE = 32;
  Block blocks[PLATFORM_BLOCKS_PER_SIDE * PLATFORM_BLOCKS_PER_SIDE];
  int blockCount = create_platform(blocks, PLATFORM_BLOCKS_PER_SIDE, CENTER,
                                   BLOCK_SIZE);

  /**
  
  
   ▄▄                                                 
  ▓▓                                                
  █▓         ▄▄▄▄           ▄▄▄▄        ▄▒ ▄▄▄▄     
 ▄██      ▄▓▀    ▀░▄     ▄▓▀    ▀░▄   ▄▒█▄▀▀   ▀▒▄  
 ▐░█     ▒▓▌      ▐▓▓   ▒▓▌      ▐▓▓   ░░▌      ▐▓█ 
  ██    ▐▓░        █▓▌ ▐▓░        █▓▌  █▓        ██▌
  ██    ▐█░        ▓▓▌ ▐█░        ▓▓▌  ▓▓        █░▌
  █▓    ▐░░        ▓░▌ ▐░░        ▓░▌  ▀▓        ░░▌
  ░░    ▐▓░         ░▌ ▐▓░         ░▌  ▒▓▌      ▐░█ 
  ░█     ▓▓▌      ▐░░   ▓▓▌      ▐░░   ▓█▀▄▄   ▄░▀  
 ░░░▌     ▀▓▄░   ▄░▀     ▀▓▄░   ▄░▀   ▄██  ▀▀▀▀     
 ▀▒░▐        ▀▀▀▀           ▀▀▀▀       ▀▒           
  
  
(loop)
  */
  // LOOP
  float lastFrame = 0.0f;
  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();

    float currentFrame = (float)glfwGetTime();
    float deltaTime = currentFrame - lastFrame;
    lastFrame = currentFrame;

    // QUIT
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
      glfwSetWindowShouldClose(window, GLFW_TRUE);


 
/**
 * Each frame applies horizontal input relative to the camera while keeping the
 * player on the ground plane, then advances vertical velocity under gravity.
 * A downward crossing of a block's top surface resolves the landing and marks
 * the player grounded; jump input is accepted only after that collision check.
 */
#pragma region Player moves physics
    update_camera(&player.camera, &mouseInput);
    PlayerInput input = read_player_input(window);
    update_player(&player, input, deltaTime, blocks, blockCount);

#pragma endregion

    glClearColor(0.15f, 0.15f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // ... rest of your rendering code ...
    glUseProgram(program);

    // Build matrices (Wider 75-degree FOV for proper first-person scale)
    mat4 proj, view, mvp;
    mat4_perspective(75.0f * (3.14159f / 180.0f), 800.0f / 600.0f, 0.1f, 100.0f,
                     proj);
    Vec3 cameraPosition = player.position;
    cameraPosition.y += player.eyeHeight;
    Vec3 target = vec3_add(cameraPosition, player.camera.front);
    mat4_lookat(cameraPosition, target, player.camera.up, view);

    glBindVertexArray(VAO);
    glBindTexture(GL_TEXTURE_2D, texture);
    for (int i = 0; i < blockCount; i++) {
      Block *block = &blocks[i];
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

      glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, (float *)mvp);
      glDrawArrays(GL_TRIANGLES, 0, 36);
    }

    glfwSwapBuffers(window);
  }

  glDeleteVertexArrays(1, &VAO);
  glDeleteBuffers(1, &VBO);

  glDeleteProgram(program);
  glfwTerminate();
  return 0;
}
