/* Reproducible CPU/submission capture. No glFinish; swaps remain interval zero.
   Run BW_PERF=1 ./performance_capture, then BW_PERF=0 with the same arguments. */
#include "../application_internal.h"
#include <stdio.h>
#include <math.h>
#include <assert.h>

static void capture(Application *a, const char *label, int traversal, int edits) {
  Performance wall = {.enabled = 1};
  performance_reset(&a->performance);
  /* Drain previous-frame work from the start-to-start raw interval. */
  a->lastFrame = glfwGetTime();
  for (int frame = 0; frame < 30; frame++) {
    double start = glfwGetTime();
    if (traversal) {
      const int step = frame < 15 ? frame : 29 - frame;
      a->player.position.x = 0.5f + (float)step * SECTION_SIZE;
      assert(player_spawn_on_surface(&a->player, &a->world));
    }
    if (edits && frame % 5 == 0) {
      /* A face shared in X and Y; alternately add/remove across section edges. */
      BlockType type = (frame / 5) % 2 ? BLOCK_TYPE_EMPTY : BLOCK_TYPE_STONE;
      assert(world_set_block(&a->world, 15, 63, 0, type));
      assert(world_set_block(&a->world, 16, 64, 0, type));
    }
    assert(application_frame(a));
    assert(glGetError() == GL_NO_ERROR);
    wall.frame.raw = glfwGetTime() - start;
    perf_record(&wall);
    /* Capture uses complete same-frame wall time, including scenario actions.
       Interactive reports use raw start-to-start time before delta clamping. */
    if (a->performance.enabled) {
      a->performance.total.raw += wall.frame.raw - a->performance.samples[frame];
      a->performance.samples[frame] = wall.frame.raw;
    }
  }
  performance_report(a, label);
  perf_finish_window(&wall);
  printf("CAPTURE phase=%s instrumentation=%d overlay=%d side=%d distance=%.0f "
         "frames=%zu wall_ms=%.3f/%.3f/%.3f\n", label, a->performance.enabled,
         a->performance.overlay, a->world.blocksPerSide, a->renderer.viewDistance,
         wall.count, wall.average*1000, wall.p95*1000, wall.maximum*1000);
}

int main(void) {
  glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
  assert(glfwInit());
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  Application a;
  double start = glfwGetTime();
  assert(application_init(&a));
  printf("CAPTURE compiler=%s seed=%u resolution=%dx%d swap_interval=0 overlay=%d\n",
         __VERSION__, a.world.terrainSeed, a.renderer.framebufferWidth,
         a.renderer.framebufferHeight, a.performance.overlay);
  printf("CAPTURE startup_wall_ms=%.3f side=%d instrumentation=%d GL_renderer=%s\n",
         (glfwGetTime()-start)*1000, a.world.blocksPerSide, a.performance.enabled,
         (const char *)glGetString(GL_RENDERER));
  const float distances[] = {25, 100, 300};
  for (size_t i = 0; i < sizeof(distances)/sizeof(*distances); i++) {
    a.renderer.viewDistance = distances[i];
    capture(&a, "distance-change", 0, 0);
    capture(&a, "stationary", 0, 0);
    capture(&a, "boundary-revisit", 1, 0);
    capture(&a, "section-edits", 0, 1);
  }
  application_shutdown(&a);
  return 0;
}
