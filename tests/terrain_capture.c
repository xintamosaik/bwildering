/* Reproducible dry-basin/material review; writes PPM frames to /tmp. */
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
  assert(create_terrain_streamed(&world, 6144, (Vec3){0}, 1, 1));
  assert(renderer_init(&r));
  r.framebufferWidth = 960;
  r.framebufferHeight = 640;
  r.viewDistance = 300;
  const BlockType types[] = {BLOCK_TYPE_DARK_STONE, BLOCK_TYPE_SAND, BLOCK_TYPE_GRAVEL,
                             BLOCK_TYPE_GRASS, BLOCK_TYPE_STONE, BLOCK_TYPE_SNOW};
  const char *names[] = {"basin", "sand", "gravel", "lowland", "mountain", "snow"};
  for (int scene = 0; scene < 6; scene++) {
    int found = 0, wx = 0, wz = 0;
    TerrainColumn column = {0};
    for (int x = -2048; x <= 2048 && !found; x += 16)
      for (int z = -2048; z <= 2048 && !found; z += 16) {
        TerrainColumn c = terrain_sample_column(1, x, z);
        if (c.material != types[scene]) continue;
        if (scene == 3 && (c.height < 8 || c.height > 80)) continue;
        if (scene == 4 && c.height < 180) continue;
        if (scene == 5 && c.height < 300) continue;
        wx = x; wz = z; column = c; found = 1;
      }
    assert(found);
    Player p = {.position = {(float)wx + 0.5f, (float)column.height + 24.0f, (float)wz + 32.5f}, .eyeHeight = 1,
                .aim = {.front = {0, -0.6f, -0.8f}}};
    /* Steeper terrain can rise above the target at the camera's offset. */
    p.position.y = fmaxf(p.position.y, world_surface_height(&world, p.position.x, p.position.z) + 12);
    p.aim.front = vec3_normalize((Vec3){0, (float)column.height - (p.position.y + p.eyeHeight), -32});
    assert(renderer_sync_view(&r, &world, &p));
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
    printf("%s: seed 1, voxel (%d, %d, %d), camera Y %.1f -> %s\n", names[scene], wx,
           column.height - 1, wz, p.position.y, path);
  }
  renderer_destroy(&r);
  world_destroy(&world);
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
