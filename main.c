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
typedef struct {
    float yaw;
    float pitch;
} Camera;

typedef struct {
    Vec3 position;
    float velocityY;
    int grounded;
    Camera camera;
} Player;

 Player player = {0};


const float gravity = 9.81f;
// --- Tiny Custom Math Backend (id Software style) ---
typedef float mat4[16];
// --- Forward Declarations ---
void mat4_identity(mat4 dest);
void mat4_perspective(float fov_radians, float aspect, float near, float far,
                      mat4 dest);
void mat4_lookat(Vec3 eye, float center[3], float up[3], mat4 dest);
void mat4_rotate(mat4 dest, float angle, float x, float y, float z);
void mat4_multiply(mat4 a, mat4 b, mat4 dest);

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

void mat4_identity(mat4 dest) {
  for (int i = 0; i < 16; i++)
    dest[i] = 0.0f;
  dest[0] = dest[5] = dest[10] = dest[15] = 1.0f;
}

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

void mat4_lookat(Vec3 eye, float center[3], float up[3], mat4 dest) {
  float f[3] = {center[0] - eye.x, center[1] - eye.y, center[2] - eye.z};
  float flen = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
  f[0] /= flen;
  f[1] /= flen;
  f[2] /= flen;

  float s[3] = {f[1] * up[2] - f[2] * up[1], f[2] * up[0] - f[0] * up[2],
                f[0] * up[1] - f[1] * up[0]};
  float slen = sqrtf(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
  s[0] /= slen;
  s[1] /= slen;
  s[2] /= slen;

  float u[3] = {s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2],
                s[0] * f[1] - s[1] * f[0]};

  mat4_identity(dest);
  dest[0] = s[0];
  dest[4] = s[1];
  dest[8] = s[2];
  dest[1] = u[0];
  dest[5] = u[1];
  dest[9] = u[2];
  dest[2] = -f[0];
  dest[6] = -f[1];
  dest[10] = -f[2];

  dest[12] = -(s[0] * eye.x + s[1] * eye.y + s[2] * eye.z);
  dest[13] = -(u[0] * eye.x + u[1] * eye.y + u[2] * eye.z);
  dest[14] = (f[0] * eye.x + f[1] * eye.y + f[2] * eye.z);
}

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
// ---------------------------

// Camera State Variables
// float cameraPos[] = {0.0f, 2.0f, 2.0f};
float cameraFront[] = {0.0f, 0.0f, -1.0f};
float cameraUp[] = {0.0f, 1.0f, 0.0f};

// Mouse Look Angles
float yaw = -90.0f;
float pitch = 0.0f;
float lastX = 400.0f;
float lastY = 300.0f;
int firstMouse = 1;

void mouse_callback(GLFWwindow *window, double xpos, double ypos) {
  (void)window;
  if (firstMouse) {
    lastX = (float)xpos;
    lastY = (float)ypos;
    firstMouse = 0;
  }

  float xoffset = (float)xpos - lastX;
  float yoffset =
      lastY - (float)ypos; // Reversed since y-coordinates go from bottom to top
  lastX = (float)xpos;
  lastY = (float)ypos;

  float sensitivity = 0.1f;
  xoffset *= sensitivity;
  yoffset *= sensitivity;

  yaw += xoffset;
  pitch += yoffset;

  if (pitch > 89.0f)
    pitch = 89.0f;
  if (pitch < -89.0f)
    pitch = -89.0f;

  float radYaw = yaw * 3.14159265f / 180.0f;
  float radPitch = pitch * 3.14159265f / 180.0f;

  cameraFront[0] = cosf(radYaw) * cosf(radPitch);
  cameraFront[1] = sinf(radPitch);
  cameraFront[2] = sinf(radYaw) * cosf(radPitch);

  // Normalize front vector
  float len =
      sqrtf(cameraFront[0] * cameraFront[0] + cameraFront[1] * cameraFront[1] +
            cameraFront[2] * cameraFront[2]);
  cameraFront[0] /= len;
  cameraFront[1] /= len;
  cameraFront[2] /= len;
}

const char *vertexShaderSource =
    "#version 330 core\n"
    "layout (location = 0) in vec3 aPos;\n"
    "layout (location = 1) in vec2 aTexCoord;\n" // Changed from color to UV
                                                 // coordinates
    "out vec2 TexCoord;\n"
    "uniform mat4 mvp;\n"
    "void main() {\n"
    "    gl_Position = mvp * vec4(aPos, 1.0);\n"
    "    TexCoord = aTexCoord;\n"
    "}\0";

const char *fragmentShaderSource =
    "#version 330 core\n"
    "in vec2 TexCoord;\n"
    "out vec4 FragColor;\n"
    "uniform sampler2D ourTexture;\n"
    "void main() {\n"
    "    FragColor = texture(ourTexture, TexCoord);\n"
    "}\0";
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
  // Set texture wrapping/filtering options
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                  GL_NEAREST); // Pixel-art style scaling!
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  glEnable(GL_DEPTH_TEST);

  // Shaders
  GLuint vShader = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vShader, 1, &vertexShaderSource, NULL);
  glCompileShader(vShader);
  GLuint fShader = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fShader, 1, &fragmentShaderSource, NULL);
  glCompileShader(fShader);
  GLuint program = glCreateProgram();
  glAttachShader(program, vShader);
  glAttachShader(program, fShader);
  glLinkProgram(program);
  glDeleteShader(vShader);
  glDeleteShader(fShader);

  // Cube Vertex Data (Grass/Green styled with slight shade variation)
  //
  // Cube Vertex Data: [PosX, PosY, PosZ, U, V]
  //
  // Cube Vertex Data (36 unindexed vertices, 6 per face, with independent UVs)
  float vertices[] = {
      // Back face
      -0.5f, -0.5f, -0.5f, 0.0f, 0.0f, 0.5f, -0.5f, -0.5f, 1.0f, 0.0f, 0.5f,
      0.5f, -0.5f, 1.0f, 1.0f, 0.5f, 0.5f, -0.5f, 1.0f, 1.0f, -0.5f, 0.5f,
      -0.5f, 0.0f, 1.0f, -0.5f, -0.5f, -0.5f, 0.0f, 0.0f,

      // Front face
      -0.5f, -0.5f, 0.5f, 0.0f, 0.0f, 0.5f, -0.5f, 0.5f, 1.0f, 0.0f, 0.5f, 0.5f,
      0.5f, 1.0f, 1.0f, 0.5f, 0.5f, 0.5f, 1.0f, 1.0f, -0.5f, 0.5f, 0.5f, 0.0f,
      1.0f, -0.5f, -0.5f, 0.5f, 0.0f, 0.0f,

      // Left face
      -0.5f, 0.5f, 0.5f, 1.0f, 0.0f, -0.5f, 0.5f, -0.5f, 1.0f, 1.0f, -0.5f,
      -0.5f, -0.5f, 0.0f, 1.0f, -0.5f, -0.5f, -0.5f, 0.0f, 1.0f, -0.5f, -0.5f,
      0.5f, 0.0f, 0.0f, -0.5f, 0.5f, 0.5f, 1.0f, 0.0f,

      // Right face
      0.5f, 0.5f, 0.5f, 1.0f, 0.0f, 0.5f, 0.5f, -0.5f, 1.0f, 1.0f, 0.5f, -0.5f,
      -0.5f, 0.0f, 1.0f, 0.5f, -0.5f, -0.5f, 0.0f, 1.0f, 0.5f, -0.5f, 0.5f,
      0.0f, 0.0f, 0.5f, 0.5f, 0.5f, 1.0f, 0.0f,

      // Bottom face
      -0.5f, -0.5f, -0.5f, 0.0f, 1.0f, 0.5f, -0.5f, -0.5f, 1.0f, 1.0f, 0.5f,
      -0.5f, 0.5f, 1.0f, 0.0f, 0.5f, -0.5f, 0.5f, 1.0f, 0.0f, -0.5f, -0.5f,
      0.5f, 0.0f, 0.0f, -0.5f, -0.5f, -0.5f, 0.0f, 1.0f,

      // Top face
      -0.5f, 0.5f, -0.5f, 0.0f, 1.0f, 0.5f, 0.5f, -0.5f, 1.0f, 1.0f, 0.5f, 0.5f,
      0.5f, 1.0f, 0.0f, 0.5f, 0.5f, 0.5f, 1.0f, 0.0f, -0.5f, 0.5f, 0.5f, 0.0f,
      0.0f, -0.5f, 0.5f, -0.5f, 0.0f, 1.0f};

  GLuint VAO, VBO;
  glGenVertexArrays(1, &VAO);
  glGenBuffers(1, &VBO);

  glBindVertexArray(VAO);
  glBindBuffer(GL_ARRAY_BUFFER, VBO);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

  // Attribute 0: Position (3 floats)
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);

  // Attribute 1: UV coordinates (2 floats)
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                        (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);
  GLint mvpLoc = glGetUniformLocation(program, "mvp");

  float lastFrame = 0.0f;

  // Generate a procedural retro checkerboard texture in memory
  unsigned int texture;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                  GL_NEAREST); // Crisp pixel art look
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  int tw = 16, th = 16;
  unsigned char *pixels = malloc(tw * th * 3);
  for (int y = 0; y < th; y++) {
    for (int x = 0; x < tw; x++) {
      // Larger 4x4 pixel checker squares eliminate moiré stripes
      int check = ((x / 4) + (y / 4)) % 2;
      int index = (y * tw + x) * 3;

      pixels[index + 0] = check ? 34 : 58;   // Red
      pixels[index + 1] = check ? 139 : 180; // Green (Retro grass)
      pixels[index + 2] = check ? 34 : 58;   // Blue
    }
  }
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, tw, th, 0, GL_RGB, GL_UNSIGNED_BYTE,
               pixels);
  free(pixels);

  while (!glfwWindowShouldClose(window)) {
    float currentFrame = (float)glfwGetTime();
    float deltaTime = currentFrame - lastFrame;
    lastFrame = currentFrame;

    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
      glfwSetWindowShouldClose(window, GLFW_TRUE);

    // --- Physics & Movement ---
    float cameraSpeed = 4.0f * deltaTime;

    // Horizontal movement vectors (ignore Y component so player stays level)
    float frontXZ[3] = {cameraFront[0], 0.0f, cameraFront[2]};
    float f_len = sqrtf(frontXZ[0] * frontXZ[0] + frontXZ[2] * frontXZ[2]);
    if (f_len > 0.0f) {
      frontXZ[0] /= f_len;
      frontXZ[2] /= f_len;
    }

    float right[3] = {frontXZ[1] * cameraUp[2] - frontXZ[2] * cameraUp[1],
                      frontXZ[2] * cameraUp[0] - frontXZ[0] * cameraUp[2],
                      frontXZ[0] * cameraUp[1] - frontXZ[1] * cameraUp[0]};
    float r_len =
        sqrtf(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
    if (r_len > 0.0f) {
      right[0] /= r_len;
      right[2] /= r_len;
    }

    // WASD Input (XZ plane only)
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
      player.position.x += frontXZ[0] * cameraSpeed;
      player.position.z += frontXZ[2] * cameraSpeed;
    }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
      player.position.x -= frontXZ[0] * cameraSpeed;
      player.position.z -= frontXZ[2] * cameraSpeed;
    }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
      player.position.x -= right[0] * cameraSpeed;
      player.position.z -= right[2] * cameraSpeed;
    }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
      player.position.x += right[0] * cameraSpeed;
      player.position.z += right[2] * cameraSpeed;
    }

    // Apply Gravity
    player.velocityY -= gravity * deltaTime;
    player.position.y += player.velocityY * deltaTime;

    // --- Simple Platform Collision ---
    float platformSurfaceY = 1.0f;

    if (player.position.x >= -4.0f && player.position.x <= 4.0f &&
        player.position.z >= -4.0f && player.position.z <= 4.0f) {

      if (player.position.y <= platformSurfaceY) {
        player.position.y = platformSurfaceY;
        player.velocityY = 0.0f;
        player.grounded = 1;
      }
    } else {
      player.grounded = 0;
    }

    // Jump on Spacebar
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS && player.grounded) {
      player.velocityY = 5.0f;
      player.grounded = 0;
    }

    glClearColor(0.15f, 0.15f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // ... rest of your rendering code ...
    glUseProgram(program);

    // Build matrices (Wider 75-degree FOV for proper first-person scale)
    mat4 proj, view, mvp;
    mat4_perspective(75.0f * (3.14159f / 180.0f), 800.0f / 600.0f, 0.1f, 100.0f,
                     proj);
    // View matrix pointing from cameraPos toward cameraPos + cameraFront
    float center[3] = {player.position.x + cameraFront[0],
                       player.position.y + cameraFront[1],
                       player.position.z + cameraFront[2]};
    mat4_lookat( player.position, center, cameraUp, view);

    glBindVertexArray(VAO);
    glBindTexture(GL_TEXTURE_2D, texture);
    // 1. Draw the 8x8x1 Platform blocks at y = -1.0f
    for (int x = -4; x < 4; x++) {
      for (int z = -4; z < 4; z++) {
        mat4 model, temp;
        mat4_identity(model);
        model[12] = (float)x + 0.5f; // X position
        model[13] = -1.0f;           // Y position (platform floor)
        model[14] = (float)z + 0.5f; // Z position

        mat4_multiply(view, model, temp);
        mat4_multiply(proj, temp, mvp);

        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, (float *)mvp);
        //        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
        // Inside the platform loop:
        glDrawArrays(GL_TRIANGLES, 0, 36);
      }
    }

    glfwSwapBuffers(window);
    glfwPollEvents();
  }

  glDeleteVertexArrays(1, &VAO);
  glDeleteBuffers(1, &VBO);

  glDeleteProgram(program);
  glfwTerminate();
  return 0;
}
