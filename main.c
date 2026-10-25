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


                                           ▄▄       ▄▄▒         
  ▄▒ ▄▄▄▄       ▄▄▄▄                        ▓▓▀   ▀▓▓           
▄▀▓▄▀▀   ▀░▄  ▄▀▀   ▀░▄         ▄▄▄▄        █▓     ▓█  ▄▄▄▄     
 ░░▌      ▐░░░       ▐▓▓     ▄░▀   ▀▀▄██▄  ▄██▄▄▄ ▄██▄▀▀   ▀░▄  
 ░         ░█▌        █▓▌   ░░▌      ▐░█   ▐░█▀    █░▌      ▐░░ 
  ▓        ██▌        ░▓▌  ▐░█        ██    ██     ██        █░▌
 ▓▓        ▒█▌        ░ ▌  ▐██        ██    ██     ██        ██▌
 ▒█        ▓▓▌        ▓▓▌  ▐█▓        █▓    █▓     ▓█        ▓█▌
 ▓░        █▓▌        ░▄▌  ▐█▓        █▓    ░░     ▓█        ▓█▌
▄█░       ▄▓▓▌       ▄░░▌   ▓▓▌      ▐██    ░█     ██       ▐▓▓ 
 ▀▄        ▀▒▌        ▀▄▌    ▀▒▄   ▄▄▀██▄  ░░░▌   ▄██       ▒▀  
   ▀░        ▀░         ▀░      ▀▀▀▀       ▀▒░▐                 


(math)

*/
typedef float mat4[16];
// --- Forward Declarations ---
void mat4_identity(mat4 dest);
void mat4_perspective(float fov_radians, float aspect, float near, float far,
                      mat4 dest);
void mat4_lookat(Vec3 eye, Vec3 center, Vec3 up, mat4 dest);
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

void mat4_lookat(Vec3 eye, Vec3 center, Vec3 up, mat4 dest) {
  float f[3] = {center.x - eye.x, center.y - eye.y, center.z - eye.z};
  float flen = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
  f[0] /= flen;
  f[1] /= flen;
  f[2] /= flen;

  float s[3] = {f[1] * up.z - f[2] * up.y, f[2] * up.x - f[0] * up.z,
                f[0] * up.y - f[1] * up.x};
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
/**




                                                                       ▄▒ ▄▄▄▄                    
                             ▄▒ ▄▄▄▄       ▄▄▄▄                      ▄▒█▄▀▀   ▀▒▄                 
     ▄▄▄▄        ▄▄▄▄      ▄▀▓▄▀▀   ▀░▄  ▄▀▀   ▀░▄         ▄▄▄▄       ░░▌      ▐▓█       ▄▄▄▄     
  ▄░▀   ▀█▄   ▄░▀   ▀▀▄██▄  ░░▌      ▐░░░       ▐▓▓     ▄░▀    ▀▒▄    █▓        ██▌   ▄░▀   ▀▀▄██▄
 ░░▌     ▀   ░░▌      ▐░█   ░         ░█▌        █▓▌   ▒░▌      ▐     ▓▓        █░▌  ░░▌      ▐░█ 
▐░█         ▐░█        ██    ▓        ██▌        ░▓▌  ▐▓         ▓ ▌  ▀▓        ░░▌ ▐░█        ██ 
▐██         ▐██        ██   ▓▓        ▒█▌        ░ ▌  ▐░░        ▓▓▌  ▒▓▌      ▐░█  ▐██        ██ 
▐█▓         ▐█▓        █▓   ▒█        ▓▓▌        ▓▓▌  ▐ ▓    ▄▄▄▄░░▌  ▓█▀▄▄   ▄░▀   ▐█▓        █▓ 
▐█▓         ▐█▓        █▓   ▓░        █▓▌        ░▄▌  ▐██▀▀▀▀▀       ▄██  ▀▀▀▀▒▄    ▐█▓        █▓ 
 ▓▓▌     ██  ▓▓▌      ▐██  ▄█░       ▄▓▓▌       ▄░░▌   ▓█▌      ▐▓▓   ▐█        ▀▄   ▓▓▌      ▐██ 
  ▀▒▄   ▄█▀   ▀▒▄   ▄▄▀██▄  ▀▄        ▀▒▌        ▀▄▌    ▀▓▄    ▄░▀     █         ▐▌   ▀▒▄   ▄▄▀██▄
     ▀▀▀▀        ▀▀▀▀         ▀░        ▀░         ▀░      ▀▀▀▀                  ▀       ▀▀▀▀     


(camera)
*/
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

  float radYaw = player.camera.yaw * 3.14159265f / 180.0f;
  float radPitch = player.camera.pitch * 3.14159265f / 180.0f;

  player.camera.front.x = cosf(radYaw) * cosf(radPitch);
  player.camera.front.y = sinf(radPitch);
  player.camera.front.z = sinf(radYaw) * cosf(radPitch);

  // Normalize front vector
  float len =
      sqrtf(player.camera.front.x * player.camera.front.x +
        player.camera.front.y * player.camera.front.y +
        player.camera.front.z * player.camera.front.z);
      player.camera.front.x /= len;
      player.camera.front.y /= len;
      player.camera.front.z /= len;
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



                ▄▄▒                                  ▒▄▄                    ▄▒ ▄▄▄▄     
              ▀▓▓                                      ▓▓▀                ▄▒█▄▀▀   ▀▒▄  
     ▄▄▄▄      ▓█  ▄▄▄▄           ▄▄▄▄           ▄▄▄▄  █▓       ▄▄▄▄       ░░▌      ▐▓█ 
  ▄▒▀    ▀░▄  ▄██▄▀▀   ▀░▄     ▄░▀   ▀▀▄██▄   ▄░▀   ▀▀▄██▄   ▄░▀    ▀▒▄    █▓        ██▌
   ▌      ▐░▒  █░▌      ▐░░   ░░▌      ▐░█   ░░▌      ▐░█   ▒░▌      ▐     ▓▓        █░▌
▐ ▓       ▀    ██        █░▌ ▐░█        ██  ▐░█        ██  ▐▓         ▓ ▌  ▀▓        ░░▌
▐▓▓▌           ██        ██▌ ▐██        ██  ▐██        ██  ▐░░        ▓▓▌  ▒▓▌      ▐░█ 
 ▀░█▄▄▄▄       ▓█        ▓█▌ ▐█▓        █▓  ▐█▓        █▓  ▐ ▓    ▄▄▄▄░░▌  ▓█▀▄▄   ▄░▀  
  ▄   ▀▀▀▀██▄  ▓█        ▓█▌ ▐█▓        █▓  ▐█▓        █▓  ▐██▀▀▀▀▀       ▄██  ▀▀▀▀▒▄   
 ▓▓▌      ▐█▓  ██       ▐▓▓   ▓▓▌      ▐██   ▓▓▌      ▐██   ▓█▌      ▐▓▓   ▐█        ▀▄ 
  ▀░▄    ▄▓▀  ▄██       ▒▀     ▀▒▄   ▄▄▀██▄   ▀▒▄   ▄▄▀██▄   ▀▓▄    ▄░▀     █         ▐▌
     ▀▀▀▀                         ▀▀▀▀           ▀▀▀▀ ▀▒█       ▀▀▀▀                  ▀ 



(shader)
*/
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

/**
                               ▄▄▒                        
                           ▀▓▓                          
     ▄▄▄▄                   ▓█  ▄▄▄▄           ▄▄▄▄     
  ▄░▀   ▀█▄   ▄▓       ▄   ▄██▄▀▀   ▀░▄     ▄░▀    ▀▒▄  
 ░░▌     ▀   ▒▓▌      ▐▓▓   █░▌      ▐░░   ▒░▌      ▐   
▐░█         ▐▓░        █▓▌  ██        █░▌ ▐▓         ▓ ▌
▐██         ▐█░        ▓▓▌  ██        ██▌ ▐░░        ▓▓▌
▐█▓         ▐░░        ▓░▌  ▓█        ▓█▌ ▐ ▓    ▄▄▄▄░░▌
▐█▓         ▐▓░         ░▌  ▓█        ▓█▌ ▐██▀▀▀▀▀      
 ▓▓▌     ██  ▓▓▌      ▐░░   ██▌      ▐▓▓   ▓█▌      ▐▓▓ 
  ▀▒▄   ▄█▀   ▀▓▄░   ▄░▀   ▄██▀▄▄   ▄▒▀     ▀▓▄    ▄░▀  
     ▀▀▀▀        ▀▀▀▀       █▒▀ ▀▀▀▀           ▀▀▀▀     

  (cube)
*/
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






  // Generate a procedural retro checkerboard texture in memory
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
    
                                                                         
  ▄▒ ▄▄▄▄       ▄▄▄▄                                                 
▄▀▓▄▀▀   ▀░▄  ▄▀▀   ▀░▄         ▄▄▄▄                        ▄▄▄▄     
 ░░▌      ▐░░░       ▐▓▓     ▄▓▀    ▀░▄     ▄▓     ▄     ▄░▀    ▀▒▄  
 ░         ░█▌        █▓▌   ▒▓▌      ▐▓▓   ▒▓▌    ▐▓▓   ▒░▌      ▐   
  ▓        ██▌        ░▓▌  ▐▓░        █▓▌ ▐▓░      █▓▌ ▐▓         ▓ ▌
 ▓▓        ▒█▌        ░ ▌  ▐█░        ▓▓▌ ▐█░      ▓▓▌ ▐░░        ▓▓▌
 ▒█        ▓▓▌        ▓▓▌  ▐░░        ▓░▌ ▐░░      ▓░▌ ▐ ▓    ▄▄▄▄░░▌
 ▓░        █▓▌        ░▄▌  ▐▓░         ░▌ ▐▓░     ▐ ░  ▐██▀▀▀▀▀      
▄█░       ▄▓▓▌       ▄░░▌   ▓▓▌      ▐░░   ▐▓▌    █░▀   ▓█▌      ▐▓▓ 
 ▀▄        ▀▒▌        ▀▄▌    ▀▓▄░   ▄░▀     ▀▓▄ ▄░▀      ▀▓▄    ▄░▀  
   ▀░        ▀░         ▀░      ▀▀▀▀          ▀█▀           ▀▀▀▀     
    
    
  (move)
    */
    // --- Physics & Movement ---
    float cameraSpeed = 4.0f * deltaTime;

    // Horizontal movement vectors (ignore Y component so player stays level)
    Vec3 frontXZ = {player.camera.front.x, 0.0f, player.camera.front.z};
    float f_len = sqrtf(frontXZ.x * frontXZ.x + frontXZ.z * frontXZ.z);
    if (f_len > 0.0f) {
      frontXZ.x /= f_len;
      frontXZ.z /= f_len;
    }

    Vec3 right = {frontXZ.y * player.camera.up.z -
                      frontXZ.z * player.camera.up.y,
                  frontXZ.z * player.camera.up.x -
                      frontXZ.x * player.camera.up.z,
                  frontXZ.x * player.camera.up.y -
                      frontXZ.y * player.camera.up.x};
    float r_len =
        sqrtf(right.x * right.x + right.y * right.y + right.z * right.z);
    if (r_len > 0.0f) {
      right.x /= r_len;
      right.z /= r_len;
    }

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
    Vec3 center = {cameraPosition.x + player.camera.front.x,
             cameraPosition.y + player.camera.front.y,
             cameraPosition.z + player.camera.front.z};
    mat4_lookat(cameraPosition, center, player.camera.up, view);

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
