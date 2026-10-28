/* Native-resolution comparison: identical camera, distance and full residency.
   Reports completed rendering wall time, including UI and buffer swaps. */
#include "../application_internal.h"
#include <assert.h>
#include <stdio.h>

static Application application;
static unsigned char pixels[1920 * 1200 * 3];

static void capture(const char *const scene, const int enabled) {
  application.renderer.distantEnabled = enabled;
  for (int frame = 0; frame < 40; frame++) application_render(&application);
  glFinish();
  Performance *const p = &application.performance;
  performance_reset(p);
  const double start = glfwGetTime();
  for (int frame = 0; frame < 180; frame++) {
    p->frame = (FrameWork){0};
    application_render(&application);
    perf_record(p);
  }
  glFinish();
  const double elapsed = glfwGetTime() - start;
  printf("DISTANT scene=%s enabled=%d resolution=%dx%d distance=%.0f "
         "frame_ms=%.3f fps=%.0f draws=%.0f triangles=%.0f coarse_bytes=%zu\n",
         scene, enabled, application.renderer.framebufferWidth, application.renderer.framebufferHeight,
         application.renderer.viewDistance, elapsed * 1000.0 / 180.0, 180.0 / elapsed,
         (double)p->total.draws / 180.0, (double)p->total.triangles / 180.0,
         enabled ? application.renderer.distant.bytes : 0u);
  fflush(stdout);
  const int width = application.renderer.framebufferWidth, height = application.renderer.framebufferHeight;
  if (width > 0 && width <= 1920 && height > 0 && height <= 1200) {
    /* World-only capture, before swapping, avoids window visibility/compositor
       effects while reviewing the same submitted scene. */
    glClearColor(0.62f, 0.72f, 0.80f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    render_world(&application.renderer, player_committed_view(&application.game.player));
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels);
    char path[128];
    snprintf(path, sizeof(path), "/tmp/distant-terrain-%s-%s.ppm", scene, enabled ? "coarse" : "full");
    FILE *const file = fopen(path, "wb");
    assert(file);
    fprintf(file, "P6\n%d %d\n255\n", width, height);
    for (int y = height - 1; y >= 0; y--)
      assert(fwrite(pixels + (size_t)y * (size_t)width * 3u, 1, (size_t)width * 3u, file) == (size_t)width * 3u);
    assert(fclose(file) == 0);
  }
  assert(glGetError() == GL_NO_ERROR);
}

int main(void) {
  glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
  assert(glfwInit());
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  assert(application_init(&application));
  application.performance.enabled = 1;
  application.renderer.viewDistance = 300.0f;
  printf("DISTANT GPU=%s\n", (const char *)glGetString(GL_RENDERER));
  static const struct { const char *name; Vec3 position; float yaw; } scenes[] = {
      {"other-island", {640.5f, 2.0f, 256.5f}, 180.0f},
      {"mountain", {682.0f, 2.0f, 170.0f}, 45.0f},
  };
  for (size_t i = 0; i < sizeof(scenes) / sizeof(*scenes); i++) {
    application.game.player.position = scenes[i].position;
    application.game.player.aim = application.pendingAim = game_aim(scenes[i].yaw, 0.0f);
    assert(application_stream_view(&application.streaming, &application.renderer, &application.game.world,
                                   &application.game.player, NULL));
    capture(scenes[i].name, 0);
    capture(scenes[i].name, 1);
  }
  application_shutdown(&application);
  return 0;
}
