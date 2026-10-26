#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "game_logic.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
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
  int chunkX, sectionY, chunkZ;
  GLuint vao, vbo;
  Vec3 boundsMin, boundsMax;
  GLint firstVertex[BLOCK_TYPE_COUNT];
  GLsizei vertexCount[BLOCK_TYPE_COUNT];
} SectionGpuMesh;

typedef struct {
  SectionGpuMesh *meshes;
  size_t meshCount;
  GLuint textVao;
  GLuint textVbo;
  GLuint program;
  GLint mvpLocation;
  GLint colorLocation;
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
  double fpsWindowStart;
  double simulationAccumulator;
  int fpsFrameCount;
  int fps;
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
static const uint32_t WORLD_SEED = UINT32_C(0x4B1D2A37);
static const double FIXED_DELTA_TIME = 1.0 / 60.0;
static const double MAX_FRAME_DELTA_TIME = 0.25;
static const int FPS_TEXT_MAX_CHARS = 16;

#pragma endregion

#pragma region Input
static void mouse_callback(GLFWwindow *window, const double position_x, const double position_y) {
  if (glfwGetInputMode(window, GLFW_CURSOR) != GLFW_CURSOR_DISABLED)
    return;
  Application *application = glfwGetWindowUserPointer(window);
  if (application == NULL)
    return;
  MouseInput *mouseInput = &application->mouseInput;
  if (mouseInput->firstMouse) {
    mouseInput->lastX = position_x;
    mouseInput->lastY = position_y;
    mouseInput->firstMouse = 0;
    return;
  }

  mouseInput->deltaX += position_x - mouseInput->lastX;
  mouseInput->deltaY += mouseInput->lastY - position_y;
  mouseInput->lastX = position_x;
  mouseInput->lastY = position_y;
}

static PlayerInput read_player_input(GLFWwindow *window) {
  PlayerInput input = {0};
  input.forward = (float)((glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) -
                          (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS));
  input.strafe = (float)((glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) -
                         (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS));
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
static GLuint compile_shader(GLenum type, const char *source) {
  const GLuint shader = glCreateShader(type);
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
      "uniform mat4 mvp;\n"
      "out vec3 worldPosition;\n"
      "void main() {\n"
      "    worldPosition = aPos;\n"
      "    gl_Position = mvp * vec4(aPos, 1.0);\n"
      "}\n";

  const char *fragmentShaderSource =
      "#version 330 core\n"
      "out vec4 FragColor;\n"
      "uniform vec3 color;\n"
      "in vec3 worldPosition;\n"
      "void main() {\n"
      "    vec3 normal = normalize(cross(dFdx(worldPosition), dFdy(worldPosition)));\n"
      "    if (!gl_FrontFacing) normal = -normal;\n"
      "    float light = 0.45 + 0.55 * max(dot(normal, normalize(vec3(-0.4, 1.0, 0.3))), 0.0);\n"
      "    FragColor = vec4(color * light, 1.0);\n"
      "}\n";

  const GLuint vertexShader = compile_shader(GL_VERTEX_SHADER, vertexShaderSource);
  if (vertexShader == 0)
    return 0;

  const GLuint fragmentShader = compile_shader(GL_FRAGMENT_SHADER, fragmentShaderSource);
  if (fragmentShader == 0) {
    glDeleteShader(vertexShader);
    return 0;
  }

  const GLuint program = glCreateProgram();
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

static void section_gpu_mesh_destroy(SectionGpuMesh *mesh) {
  if (mesh->vao)
    glDeleteVertexArrays(1, &mesh->vao);
  if (mesh->vbo)
    glDeleteBuffers(1, &mesh->vbo);
  *mesh = (SectionGpuMesh){0};
}

static void renderer_destroy(Renderer *renderer) {
  for (size_t i = 0; i < renderer->meshCount; i++)
    section_gpu_mesh_destroy(&renderer->meshes[i]);
  free(renderer->meshes);
  if (renderer->textVao != 0)
    glDeleteVertexArrays(1, &renderer->textVao);
  if (renderer->textVbo != 0)
    glDeleteBuffers(1, &renderer->textVbo);
  if (renderer->program != 0)
    glDeleteProgram(renderer->program);
  *renderer = (Renderer){0};
}

static int renderer_init(Renderer *renderer) {
  *renderer = (Renderer){0};
  renderer->program = create_shader_program();
  if (renderer->program == 0)
    return 0;

  glGenVertexArrays(1, &renderer->textVao);
  glGenBuffers(1, &renderer->textVbo);
  if (renderer->textVao == 0 || renderer->textVbo == 0) {
    renderer_destroy(renderer);
    return 0;
  }
  glBindVertexArray(renderer->textVao);
  glBindBuffer(GL_ARRAY_BUFFER, renderer->textVbo);
  glBufferData(GL_ARRAY_BUFFER,
               (GLsizeiptr)(FPS_TEXT_MAX_CHARS * 5 * 7 * 6 * 3 * sizeof(float)), NULL,
               GL_DYNAMIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float),
                        (void *)0);
  glEnableVertexAttribArray(0);

  renderer->mvpLocation = glGetUniformLocation(renderer->program, "mvp");
  if (renderer->mvpLocation < 0) {
    fprintf(stderr, "Failed to locate mvp uniform\n");
    renderer_destroy(renderer);
    return 0;
  }
  renderer->colorLocation = glGetUniformLocation(renderer->program, "color");
  if (renderer->colorLocation < 0) {
    fprintf(stderr, "Failed to locate color uniform\n");
    renderer_destroy(renderer);
    return 0;
  }

  return 1;
}

/* Upload into a temporary object: failures retain the previous GPU mesh. */
static int renderer_upload_section_mesh(SectionGpuMesh *target,
                                        const SectionMesh *mesh) {
  if (!mesh || (mesh->vertexCount && !mesh->vertices) ||
      mesh->vertexCount > (size_t)PTRDIFF_MAX / sizeof(*mesh->vertices)) return 0;
  SectionGpuMesh result = {
      .chunkX = target->chunkX, .sectionY = target->sectionY, .chunkZ = target->chunkZ};
  for (int type = 0; type < BLOCK_TYPE_COUNT; type++) {
    if (mesh->firstVertex[type] > INT_MAX || mesh->typeVertexCount[type] > INT_MAX)
      return 0;
    result.firstVertex[type] = (GLint)mesh->firstVertex[type];
    result.vertexCount[type] = (GLsizei)mesh->typeVertexCount[type];
  }
  if (mesh->vertexCount) {
    result.boundsMin = result.boundsMax =
        (Vec3){mesh->vertices[0].x, mesh->vertices[0].y, mesh->vertices[0].z};
    for (size_t i = 1; i < mesh->vertexCount; i++) {
      const SurfaceVertex v = mesh->vertices[i];
      result.boundsMin.x = fminf(result.boundsMin.x, v.x);
      result.boundsMin.y = fminf(result.boundsMin.y, v.y);
      result.boundsMin.z = fminf(result.boundsMin.z, v.z);
      result.boundsMax.x = fmaxf(result.boundsMax.x, v.x);
      result.boundsMax.y = fmaxf(result.boundsMax.y, v.y);
      result.boundsMax.z = fmaxf(result.boundsMax.z, v.z);
    }
    glGenVertexArrays(1, &result.vao);
    glGenBuffers(1, &result.vbo);
    if (!result.vao || !result.vbo) {
      section_gpu_mesh_destroy(&result);
      return 0;
    }
    glBindVertexArray(result.vao);
    glBindBuffer(GL_ARRAY_BUFFER, result.vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(mesh->vertexCount * sizeof(*mesh->vertices)),
                 mesh->vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SurfaceVertex), (void *)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    if (glGetError() != GL_NO_ERROR) {
      section_gpu_mesh_destroy(&result);
      return 0;
    }
  }
  section_gpu_mesh_destroy(target);
  *target = result;
  return 1;
}

static int renderer_sync_world(Renderer *renderer, World *world) {
  size_t count = world->chunkCount * WORLD_SECTION_COUNT;
  if (!renderer->meshes && count) {
    renderer->meshes = calloc(count, sizeof(*renderer->meshes));
    if (!renderer->meshes)
      return 0;
    renderer->meshCount = count;
    for (size_t c = 0; c < world->chunkCount; c++)
      for (int s = 0; s < WORLD_SECTION_COUNT; s++) {
        SectionGpuMesh *mesh = &renderer->meshes[c * WORLD_SECTION_COUNT + s];
        mesh->chunkX = world->chunks[c].x;
        mesh->chunkZ = world->chunks[c].z;
        mesh->sectionY = WORLD_MIN_SECTION_Y + s;
      }
  }
  if (renderer->meshCount != count)
    return 0;
  for (size_t i = 0; i < count; i++) {
    SectionGpuMesh *gpu = &renderer->meshes[i];
    Subchunk *section = world_section(world, gpu->chunkX, gpu->sectionY, gpu->chunkZ);
    if (!section) {
      /* A regenerated world can omit a section that previously had a mesh. */
      section_gpu_mesh_destroy(gpu);
      gpu->chunkX = world->chunks[i / WORLD_SECTION_COUNT].x;
      gpu->chunkZ = world->chunks[i / WORLD_SECTION_COUNT].z;
      gpu->sectionY = WORLD_MIN_SECTION_Y + (int)(i % WORLD_SECTION_COUNT);
      continue;
    }
    if (!section->dirty)
      continue;
    SectionMesh cpu = {0};
    int success =
        section_mesh_build(world, gpu->chunkX, gpu->sectionY, gpu->chunkZ, &cpu) &&
        renderer_upload_section_mesh(gpu, &cpu);
    section_mesh_destroy(&cpu);
    if (!success)
      return 0;
    section->dirty = 0;
  }
  return 1;
}

static const unsigned char *font_glyph(const char character) {
  static const unsigned char glyph0[7] = {0x0e, 0x11, 0x13, 0x15,
                                          0x19, 0x11, 0x0e};
  static const unsigned char glyph1[7] = {0x04, 0x0c, 0x04, 0x04,
                                          0x04, 0x04, 0x0e};
  static const unsigned char glyph2[7] = {0x0e, 0x11, 0x01, 0x02,
                                          0x04, 0x08, 0x1f};
  static const unsigned char glyph3[7] = {0x1e, 0x01, 0x01, 0x0e,
                                          0x01, 0x01, 0x1e};
  static const unsigned char glyph4[7] = {0x02, 0x06, 0x0a, 0x12,
                                          0x1f, 0x02, 0x02};
  static const unsigned char glyph5[7] = {0x1f, 0x10, 0x10, 0x1e,
                                          0x01, 0x01, 0x1e};
  static const unsigned char glyph6[7] = {0x0e, 0x10, 0x10, 0x1e,
                                          0x11, 0x11, 0x0e};
  static const unsigned char glyph7[7] = {0x1f, 0x01, 0x02, 0x04,
                                          0x08, 0x08, 0x08};
  static const unsigned char glyph8[7] = {0x0e, 0x11, 0x11, 0x0e,
                                          0x11, 0x11, 0x0e};
  static const unsigned char glyph9[7] = {0x0e, 0x11, 0x11, 0x0f,
                                          0x01, 0x01, 0x0e};
  static const unsigned char glyphF[7] = {0x1f, 0x10, 0x10, 0x1e,
                                          0x10, 0x10, 0x10};
  static const unsigned char glyphP[7] = {0x1e, 0x11, 0x11, 0x1e,
                                          0x10, 0x10, 0x10};
  static const unsigned char glyphS[7] = {0x0f, 0x10, 0x10, 0x0e,
                                          0x01, 0x01, 0x1e};
  static const unsigned char glyphColon[7] = {0x00, 0x04, 0x04, 0x00,
                                              0x04, 0x04, 0x00};

  switch (character) {
  case '0': return glyph0;
  case '1': return glyph1;
  case '2': return glyph2;
  case '3': return glyph3;
  case '4': return glyph4;
  case '5': return glyph5;
  case '6': return glyph6;
  case '7': return glyph7;
  case '8': return glyph8;
  case '9': return glyph9;
  case 'F': return glyphF;
  case 'P': return glyphP;
  case 'S': return glyphS;
  case ':': return glyphColon;
  default: return NULL;
  }
}

static int build_text_vertices(const char *text, const float shadowOffset,
                               const int framebufferWidth, const int framebufferHeight,
                               float *vertices) {
  int vertexCount = 0;

  for (int characterIndex = 0; text[characterIndex] != '\0';
       characterIndex++) {
    const unsigned char *glyph = font_glyph(text[characterIndex]);
    if (glyph == NULL)
      continue;
    const float originX = 16.0f + (float)characterIndex * 18.0f + shadowOffset;
    const float originY = 16.0f + shadowOffset;
    for (int row = 0; row < 7; row++) {
      for (int column = 0; column < 5; column++) {
        if ((glyph[row] & (1u << (4 - column))) == 0)
          continue;
        const float pixelSize = 2.0f;
        const float cellSize = 3.0f;
        const float left = originX + (float)column * cellSize;
        const float top = originY + (float)row * cellSize;
        const float right = left + pixelSize;
        const float bottom = top + pixelSize;
        const float quad[6][2] = {{left, top}, {right, top}, {right, bottom},
                                  {left, top}, {right, bottom}, {left, bottom}};
        for (int vertex = 0; vertex < 6; vertex++) {
          vertices[vertexCount * 3] =
              2.0f * quad[vertex][0] / (float)framebufferWidth - 1.0f;
          vertices[vertexCount * 3 + 1] =
              1.0f - 2.0f * quad[vertex][1] / (float)framebufferHeight;
          vertices[vertexCount * 3 + 2] = 0.0f;
          vertexCount++;
        }
      }
    }
  }
  return vertexCount;
}

static void render_fps(const Renderer *renderer, const int fps) {
  if (renderer->framebufferWidth <= 0 || renderer->framebufferHeight <= 0)
    return;

  char text[16];
  snprintf(text, sizeof(text), "FPS: %d", fps);
  float vertices[FPS_TEXT_MAX_CHARS * 5 * 7 * 6 * 3];
  mat4 identity;
  mat4_identity(identity);

  glUseProgram(renderer->program);
  glUniformMatrix4fv(renderer->mvpLocation, 1, GL_FALSE, (float *)identity);
  glBindVertexArray(renderer->textVao);
  glBindBuffer(GL_ARRAY_BUFFER, renderer->textVbo);
  glDisable(GL_DEPTH_TEST);

  int vertexCount = build_text_vertices(
      text, 1.0f, renderer->framebufferWidth, renderer->framebufferHeight,
      vertices);
  glBufferSubData(GL_ARRAY_BUFFER, 0,
                  (GLsizeiptr)(vertexCount * 3 * sizeof(float)), vertices);
  glUniform3f(renderer->colorLocation, 0.02f, 0.03f, 0.02f);
  glDrawArrays(GL_TRIANGLES, 0, vertexCount);

  vertexCount = build_text_vertices(
      text, 0.0f, renderer->framebufferWidth, renderer->framebufferHeight,
      vertices);
  glBufferSubData(GL_ARRAY_BUFFER, 0,
                  (GLsizeiptr)(vertexCount * 3 * sizeof(float)), vertices);
  glUniform3f(renderer->colorLocation, 0.95f, 0.98f, 0.92f);
  glDrawArrays(GL_TRIANGLES, 0, vertexCount);

  glEnable(GL_DEPTH_TEST);
}

/* Test the most positive AABB vertex against each homogeneous clip plane.
   No normalization is needed, and boxes crossing a plane remain visible. */
static int section_in_frustum(const SectionGpuMesh *mesh, const mat4 mvp) {
  for(int axis=0;axis<3;axis++) for(int sign=-1;sign<=1;sign+=2) {
    float a=mvp[3]+sign*mvp[axis];
    float b=mvp[7]+sign*mvp[4+axis];
    float c=mvp[11]+sign*mvp[8+axis];
    float d=mvp[15]+sign*mvp[12+axis];
    float x=a>=0 ? mesh->boundsMax.x : mesh->boundsMin.x;
    float y=b>=0 ? mesh->boundsMax.y : mesh->boundsMin.y;
    float z=c>=0 ? mesh->boundsMax.z : mesh->boundsMin.z;
    if(a*x+b*y+c*z+d<0) return 0;
  }
  return 1;
}

static void render_world(const Renderer *renderer, const Player *player) {
  if (renderer->framebufferWidth <= 0 || renderer->framebufferHeight <= 0)
    return;

  glUseProgram(renderer->program);

  mat4 proj, view, mvp;
  const float aspectRatio =
      (float)renderer->framebufferWidth / (float)renderer->framebufferHeight;
  mat4_perspective(75.0f * (3.14159f / 180.0f), aspectRatio, 0.05f, 100.0f, proj);
  Vec3 cameraPosition = player->position;
  cameraPosition.y += player->eyeHeight;
  const Vec3 target = vec3_add(cameraPosition, player->camera.front);
  mat4_lookat(cameraPosition, target, player->camera.up, view);
  mat4_multiply(proj, view, mvp);

  glUniformMatrix4fv(renderer->mvpLocation, 1, GL_FALSE, (float *)mvp);
  for (int type = 0; type < BLOCK_TYPE_COUNT; type++) {
    Vec3 color;
    if (!block_type_color((BlockType)type, &color))
      continue;
    glUniform3f(renderer->colorLocation, color.x, color.y, color.z);
    for (size_t i = 0; i < renderer->meshCount; i++) {
      const SectionGpuMesh *mesh = &renderer->meshes[i];
      if (!mesh->vertexCount[type] || !section_in_frustum(mesh, mvp))
        continue;
      glBindVertexArray(mesh->vao);
      glDrawArrays(GL_TRIANGLES, mesh->firstVertex[type], mesh->vertexCount[type]);
    }
  }
}

#pragma endregion

#pragma region Application
static void framebuffer_size_callback(GLFWwindow *window, const int width,
                                      const int height) {
  Application *application = glfwGetWindowUserPointer(window);
  if (application == NULL)
    return;
  application->renderer.framebufferWidth = width;
  application->renderer.framebufferHeight = height;
  glViewport(0, 0, width, height);
}

static void application_shutdown(Application *application) {
  world_destroy(&application->world);
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
      .player = {.position = {.x = 0.5f, .y = 0.0f, .z = 0.5f},
                 .grounded = 1,
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
  glfwWindowHint(GLFW_SAMPLES, 4);

  application->window =
      glfwCreateWindow(1920, 1200, "bwildering 0.1", NULL, NULL);
  if (application->window == NULL) {
    fprintf(stderr, "Failed to create GLFW window\n");
    application_shutdown(application);
    return 0;
  }
  glfwMakeContextCurrent(application->window);
  // Keep VSync off while measuring the uncapped development baseline.
  glfwSwapInterval(0);
  // For synchronized gameplay, switch the two swap-interval lines:
  // glfwSwapInterval(1);

  glfwSetWindowUserPointer(application->window, application);
  glfwSetCursorPosCallback(application->window, mouse_callback);
  glfwSetInputMode(application->window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

  glewExperimental = GL_TRUE;
  const GLenum glewStatus = glewInit();
  if (glewStatus != GLEW_OK) {
    fprintf(stderr, "Failed to initialize GLEW: %s\n",
            (const char *)glewGetErrorString(glewStatus));
    application_shutdown(application);
    return 0;
  }

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_MULTISAMPLE);

  if (!renderer_init(&application->renderer)) {
    application_shutdown(application);
    return 0;
  }
  application->rendererInitialized = 1;

  glfwGetFramebufferSize(application->window,
                          &application->renderer.framebufferWidth,
                          &application->renderer.framebufferHeight);
  glViewport(0, 0, application->renderer.framebufferWidth,
             application->renderer.framebufferHeight);
  glfwSetFramebufferSizeCallback(application->window,
                                 framebuffer_size_callback);

  if (!create_terrain(&application->world, PLATFORM_BLOCKS_PER_SIDE, CENTER,
                       BLOCK_SIZE, WORLD_SEED)) {
    fprintf(stderr, "Failed to create voxel terrain\n");
    application_shutdown(application);
    return 0;
  }
  application->player.position.y = world_surface_height(
      &application->world, application->player.position.x, application->player.position.z);
  if (!renderer_sync_world(&application->renderer,&application->world)) {
    fprintf(stderr,"Failed to build or upload section meshes\n");
    application_shutdown(application);
    return 0;
  }

  application->lastFrame = glfwGetTime();
  application->fpsWindowStart = application->lastFrame;
  return 1;
}

static void application_update(Application *application) {
  GLFWwindow *window = application->window;
  glfwPollEvents();

  const double currentFrame = glfwGetTime();
  double deltaTime = currentFrame - application->lastFrame;
  application->lastFrame = currentFrame;
  if (deltaTime < 0.0)
    deltaTime = 0.0;
  if (deltaTime > MAX_FRAME_DELTA_TIME)
    deltaTime = MAX_FRAME_DELTA_TIME;

  application->fpsFrameCount++;
  const double fpsElapsed = currentFrame - application->fpsWindowStart;
  if (fpsElapsed >= 1.0) {
    application->fps = (int)(application->fpsFrameCount / fpsElapsed + 0.5);
    application->fpsFrameCount = 0;
    application->fpsWindowStart = currentFrame;
  }

  const int escapeIsDown = glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
  if (escapeIsDown && !application->escapeWasDown) {
    const int cursorCaptured =
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
  render_world(&application->renderer, &application->player);
  render_fps(&application->renderer, application->fps);
  glfwSwapBuffers(application->window);
}

#pragma endregion

#pragma region Main
int main(void) {
  Application application;
  if (!application_init(&application))
    return EXIT_FAILURE;

  int meshRebuildFailed = 0;
  while (!glfwWindowShouldClose(application.window)) {
    application_update(&application);
    if (!renderer_sync_world(&application.renderer, &application.world)) {
      if (!meshRebuildFailed)
        fprintf(stderr, "Failed to rebuild dirty section meshes; retaining previous "
                        "meshes and retrying\n");
      meshRebuildFailed = 1;
    } else {
      meshRebuildFailed = 0;
    }
    application_render(&application);
  }

  application_shutdown(&application);
  return EXIT_SUCCESS;
}
#pragma endregion
