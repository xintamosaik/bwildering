/* Reproducible flat-foundation views; writes PPM frames to /tmp. */
#include "../world_generation.h"
#include "../application_internal.h"
#include <stdio.h>
#include <math.h>
#include <assert.h>

int main(void) {
  glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
  assert(glfwInit());
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  GLFWwindow *window = glfwCreateWindow(960, 640, "Terrain review", NULL, NULL);
  assert(window);
  glfwMakeContextCurrent(window);
  assert(glewInit() == GLEW_OK);
  while (glGetError() != GL_NO_ERROR) {}
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
  glViewport(0, 0, 960, 640);
  World world = {0};
  Renderer r = {0};
  WorldStreaming rStreaming = {0};
  assert(create_terrain_streamed(&world, 6144, (Vec3){0}, 1, 1));
  assert(renderer_init(&r));
  r.framebufferWidth = 960;
  r.framebufferHeight = 640;
  r.viewDistance = 300;
  const char *const names[] = {"sea-level", "seabed", "edge"};
  const Player views[] = {
      {.position = {0.5f, 0.0f, 0.5f}, .eyeHeight = 1.0f,
       .aim = {.front = {0.0f, -0.6f, -0.8f}}},
      {.position = {0.5f, -30.0f, 0.5f}, .eyeHeight = 1.0f,
       .aim = {.front = {0.0f, -0.2f, -0.98f}}},
      {.position = {3088.0f, -48.0f, 0.5f}, .eyeHeight = 1.0f,
       .aim = {.front = {-0.8f, -0.6f, 0.0f}}},
  };
  for (size_t scene = 0; scene < sizeof(views) / sizeof(*views); scene++) {
    const Player p = views[scene];
    assert(application_stream_view(&rStreaming, &r, &world, &p, NULL));
    glClearColor(0.62f, 0.72f, 0.80f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    render_world(&r, player_committed_view(&p));
    unsigned char pixels[960 * 640 * 3];
    glReadPixels(0, 0, 960, 640, GL_RGB, GL_UNSIGNED_BYTE, pixels);
    assert(glGetError() == GL_NO_ERROR);
    char path[128];
    snprintf(path, sizeof(path), "/tmp/world-generation-%s.ppm", names[scene]);
    FILE *file = fopen(path, "wb");
    assert(file);
    fprintf(file, "P6\n960 640\n255\n");
    for (int y = 639; y >= 0; y--) assert(fwrite(pixels + y * 960 * 3, 1, 960 * 3, file) == 960 * 3);
    fclose(file);
    printf("%s: camera (%.1f, %.1f, %.1f) -> %s\n", names[scene],
           p.position.x, p.position.y, p.position.z, path);
  }
  renderer_destroy(&r);
  world_destroy(&world);
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
