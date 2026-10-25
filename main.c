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
  Vec3 position;
  Vec3 size;
} Block;

const Vec3 CENTER = {0.0f, 0.0f, 0.0f};
const float GRAVITY = 9.81f;
const float BLOCK_SIZE = 1.0f;

Player player = {.position = {.x = 0.0f, .y = 2.0f, .z = 2.0f},
                 .eyeHeight = 1.0f,
                 .camera = {.yaw = -90.0f,
                            .pitch = 0.0f,
                            .front = {.x = 0.0f, .y = 0.0f, .z = -1.0f},
                            .up = {.x = 0.0f, .y = 1.0f, .z = 0.0f},
                            .lastX = 400.0f,
                            .lastY = 300.0f,
                            .firstMouse = 1}};

/**
 * Math
 */ 
#pragma region Math
typedef float mat4[16];
// --- Forward Declarations ---
void mat4_identity(mat4 dest);
void mat4_perspective(float fov_radians, float aspect, float near, float far,
                      mat4 dest);
void mat4_lookat(Vec3 eye, Vec3 target, Vec3 up, mat4 dest);
void mat4_rotate(mat4 dest, float angle, float x, float y, float z);
void mat4_multiply(mat4 a, mat4 b, mat4 dest);

Vec3 vec3_add(Vec3 a, Vec3 b) {
  return (Vec3){a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 vec3_sub(Vec3 a, Vec3 b) {
  return (Vec3){a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 vec3_scale(Vec3 v, float scalar) {
  return (Vec3){v.x * scalar, v.y * scalar, v.z * scalar};
}

Vec3 vec3_normalize(Vec3 v) {
  float length = sqrtf(vec3_dot(v, v));
  if (length == 0.0f)
    return (Vec3){0.0f, 0.0f, 0.0f};
  return vec3_scale(v, 1.0f / length);
}

Vec3 vec3_cross(Vec3 a, Vec3 b) {
  return (Vec3){a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x};
}

float vec3_dot(Vec3 a, Vec3 b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}


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
 * camera
*/
#pragma region camera
void update_camera_front(Camera *camera) {
  float radYaw = camera->yaw * 3.14159265f / 180.0f;
  float radPitch = camera->pitch * 3.14159265f / 180.0f;

  camera->front.x = cosf(radYaw) * cosf(radPitch);
  camera->front.y = sinf(radPitch);
  camera->front.z = sinf(radYaw) * cosf(radPitch);
  camera->front = vec3_normalize(camera->front);
}

void mouse_callback(GLFWwindow *window, double xpos, double ypos) {
  (void)window;
  if (player.camera.firstMouse) {
    player.camera.lastX = (float)xpos;
    player.camera.lastY = (float)ypos;
    player.camera.firstMouse = 0;
  }

  float xoffset = (float)xpos - player.camera.lastX;
  float yoffset =
      player.camera.lastY - (float)ypos; // Reversed since y-coordinates go from bottom to top
  player.camera.lastX = (float)xpos;
  player.camera.lastY = (float)ypos;

  float sensitivity = 0.1f;
  xoffset *= sensitivity;
  yoffset *= sensitivity;

  player.camera.yaw += xoffset;
  player.camera.pitch += yoffset;

  if (player.camera.pitch > 89.0f)
    player.camera.pitch = 89.0f;
  if (player.camera.pitch < -89.0f)
    player.camera.pitch = -89.0f;

  update_camera_front(&player.camera);
}

#pragma endregion



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

/**
 * 
 */ 
#pragma region shader
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

#pragma endregion
 
/**
 * 
 */
#pragma region cube
  GLuint VAO, VBO;
  create_cube_mesh(&VAO, &VBO);
  GLint mvpLoc = glGetUniformLocation(program, "mvp");






  // Generate a procedural retro checkerboard texture in memory
#pragma endregion

/**


 ▄▄                                    ▄▄                      ▄▒ ▄▄▄▄                    
  ▓▓▀                                   ▓▓▀                  ▄▒█▄▀▀   ▀▒▄                 
  █▓         ▄▄▄▄                       █▓                    ░░▌      ▐▓█       ▄▄▄▄     
 ▄██▄▄▄   ▄░▀    ▀▒▄     ░▄       ▄░   ▄██▄▄▄   ▄▓       ▄    █▓        ██▌   ▄░▀    ▀▒▄  
 ▐░█▀    ▒░▌      ▐     ▐░░▌      ▐░░  ▐░█▀    ▒▓▌      ▐▓▓   ▓▓        █░▌  ▒░▌      ▐   
  ██    ▐▓         ▓ ▌   ▀░▄     ▄░▀▀   ██    ▐▓░        █▓▌  ▀▓        ░░▌ ▐▓         ▓ ▌
  ██    ▐░░        ▓▓▌     ▀▄▀▄▀▄▀▀     ██    ▐█░        ▓▓▌  ▒▓▌      ▐░█  ▐░░        ▓▓▌
  █▓    ▐ ▓    ▄▄▄▄░░▌    ▄░▀   ▀░▄     █▓    ▐░░        ▓░▌  ▓█▀▄▄   ▄░▀   ▐ ▓    ▄▄▄▄░░▌
  ░░    ▐██▀▀▀▀▀         ░░▌     ▐░░    ░░    ▐▓░         ░▌ ▄██  ▀▀▀▀▒▄    ▐██▀▀▀▀▀      
  ░█     ▓█▌      ▐▓▓   ▐░█       █░▌   ░█     ▓▓▌      ▐░░   ▐█        ▀▄   ▓█▌      ▐▓▓ 
 ░░░▌     ▀▓▄    ▄░▀     ▓▓▌     ▐▓▓   ░░░▌     ▀▓▄░   ▄░▀     █         ▐▌   ▀▓▄    ▄░▀  
 ▀▒░▐        ▀▀▀▀         ▀▒     ▒▀    ▀▒░▐        ▀▀▀▀                  ▀       ▀▀▀▀     


(texture)
*/
  GLuint texture = create_grass_texture();
/**




  ▄▄▒           ▄▄                                  ▄▄▒                        
▀▓▓              ▓▓                               ▀▓▓          ▄░              
 ▓█  ▄▄▄▄        █▓         ▄▄▄▄           ▄▄▄▄    ▓█         ▐░░      ▄▄▄▄    
▄██▄▀▀   ▀░▄    ▄██      ▄▓▀    ▀░▄     ▄░▀   ▀█▄ ▄██        ▄░▀    ▄▒▀    ▀░▄ 
 █░▌      ▐░░   ▐░█     ▒▓▌      ▐▓▓   ░░▌     ▀   █░  ▄▄▄▄▀▀        ▌      ▐░▒
 ██        █░▌   ██    ▐▓░        █▓▌ ▐░█          ██▄▀▀   ▀░▄    ▐ ▓       ▀  
 ██        ██▌   ██    ▐█░        ▓▓▌ ▐██          ██▌      ▐░░   ▐▓▓▌         
 ▓█        ▓█▌   █▓    ▐░░        ▓░▌ ▐█▓          ▓█        █░▌   ▀░█▄▄▄▄     
 ▓█        ▓█▌   ░░    ▐▓░         ░▌ ▐█▓          ▓█        ██▌    ▄   ▀▀▀▀██▄
 ██▌      ▐▓▓    ░█     ▓▓▌      ▐░░   ▓▓▌     ██  ██        ▓█▌   ▓▓▌      ▐█▓
▄██▀▄▄   ▄▒▀    ░░░▌     ▀▓▄░   ▄░▀     ▀▒▄   ▄█▀ ▄██        ▓█▌    ▀░▄    ▄▓▀ 
 █▒▀ ▀▀▀▀       ▀▒░▐        ▀▀▀▀           ▀▀▀▀             ▐▓▓        ▀▀▀▀    




 
  (blocks)
*/
  const int PLATFORM_BLOCKS_PER_SIDE = 8;
  Block blocks[PLATFORM_BLOCKS_PER_SIDE * PLATFORM_BLOCKS_PER_SIDE];
  int blockCount = 0;
  for (int x = 0; x < PLATFORM_BLOCKS_PER_SIDE; x++) {
    for (int z = 0; z < PLATFORM_BLOCKS_PER_SIDE; z++) {
      blocks[blockCount++] = (Block){
          .position =
              {
                  .x = (float)x - PLATFORM_BLOCKS_PER_SIDE / 2.0f +
                       BLOCK_SIZE / 2.0f,
                  .y = CENTER.y - BLOCK_SIZE / 2.0f,
                  .z = (float)z - PLATFORM_BLOCKS_PER_SIDE / 2.0f +
                       BLOCK_SIZE / 2.0f,
              },
          .size = {BLOCK_SIZE, BLOCK_SIZE, BLOCK_SIZE},
      };
    }
  }

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
    float currentFrame = (float)glfwGetTime();
    float deltaTime = currentFrame - lastFrame;
    lastFrame = currentFrame;

    // QUIT
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
      glfwSetWindowShouldClose(window, GLFW_TRUE);


 
/**
 * Movement
 */
#pragma region movement
    // --- Physics & Movement ---
    float cameraSpeed = 4.0f * deltaTime;

    // Horizontal movement vectors (ignore Y component so player stays level)
    Vec3 frontXZ = vec3_normalize(
      (Vec3){player.camera.front.x, 0.0f, player.camera.front.z});
    Vec3 right = vec3_normalize(vec3_cross(frontXZ, player.camera.up));

    // WASD Input (XZ plane only)
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
      player.position.x += frontXZ.x * cameraSpeed;
      player.position.z += frontXZ.z * cameraSpeed;
    }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
      player.position.x -= frontXZ.x * cameraSpeed;
      player.position.z -= frontXZ.z * cameraSpeed;
    }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
      player.position.x -= right.x * cameraSpeed;
      player.position.z -= right.z * cameraSpeed;
    }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
      player.position.x += right.x * cameraSpeed;
      player.position.z += right.z * cameraSpeed;
    }

    float previousY = player.position.y;
    player.grounded = 0;

    // Apply Gravity
    player.velocityY -= GRAVITY * deltaTime;
    player.position.y += player.velocityY * deltaTime;

    for (int i = 0; i < blockCount; i++) {
      Block *block = &blocks[i];
      float halfWidth = block->size.x / 2.0f;
      float halfDepth = block->size.z / 2.0f;
      float blockTop = block->position.y + block->size.y / 2.0f;

      if (player.position.x >= block->position.x - halfWidth &&
          player.position.x <= block->position.x + halfWidth &&
          player.position.z >= block->position.z - halfDepth &&
          player.position.z <= block->position.z + halfDepth &&
          previousY >= blockTop && player.position.y <= blockTop) {
        player.position.y = blockTop;
        player.velocityY = 0.0f;
        player.grounded = 1;
        break;
      }
    }

    // Jump on Spacebar
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS && player.grounded) {
      player.velocityY = 5.0f;
      player.grounded = 0;
    }

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
    glfwPollEvents();
  }

  glDeleteVertexArrays(1, &VAO);
  glDeleteBuffers(1, &VBO);

  glDeleteProgram(program);
  glfwTerminate();
  return 0;
}
