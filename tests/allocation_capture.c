/* Opt-in, serial real-context allocation capture; no per-allocation clocks. */
#include "renderer_test.h"
#include <assert.h>
#include <sys/resource.h>
static int compare(const void *const a, const void *const b) {
  const double x = *(const double *)a, y = *(const double *)b;
  return (x > y) - (x < y);
}
static int reject_mesh(const StorageCategory category) { return category == STORAGE_MESH; }
int main(void) {
  glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
  assert(glfwInit());
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  GLFWwindow *const window = glfwCreateWindow(320, 240, "allocation capture", NULL, NULL);
  assert(window);
  glfwMakeContextCurrent(window);
  glewExperimental = GL_TRUE;
  assert(glewInit() == GLEW_OK);
  while (glGetError() != GL_NO_ERROR) {}
  storage_accounting(1);
  World world = {0};
  Renderer renderer = {0};
  WorldStreaming rendererStreaming = {0};
  const double start = glfwGetTime();
  assert(create_terrain_streamed(&world, PLATFORM_BLOCKS_PER_SIDE, (Vec3){0}, 1.0f, 1260202551));
#ifndef STORAGE_BASELINE
  assert(world_stream_storage_limits(&world, MAX_VIEW_DISTANCE));
#endif
  assert(renderer_init(&renderer));
  printf("seed=1260202551 compiler=%s GL=%s init_ms=%.6f\n", __VERSION__,
         (const char *)glGetString(GL_RENDERER), (glfwGetTime() - start) * 1000.0);
  renderer.viewDistance = 25.0f;
  Player player = {.position = {0.5f, 410.0f, 0.5f}};
  const char *const scenarios[] = {"converge", "stationary", "cold", "movement", "edits", "overlay", "maximum", "failure_retry", "repeat"};
  const char *const categories[] = {"world", "node", "payload", "mesh", "records", "visible", "draw"};
  puts("scenario,category,attempts,requested,live,peak_live,capacity,peak_capacity,waste,system_allocations,reuse,failures,p50_ms,p95_ms,p99_ms,max_ms,rss_peak_kib,gpu_requested");
  for (int scenario = 0; scenario < 9; scenario++) {
    StorageStats before[STORAGE_CATEGORY_COUNT];
    for (int c = 0; c < STORAGE_CATEGORY_COUNT; c++) before[c] = *storage_stats((StorageCategory)c);
    double times[32];
    for (int f = 0; f < 32; f++) {
      if (scenario == 2 || scenario == 8) player.position.x = -1000.5f + (float)(f % 16) * 96.0f;
      if (scenario == 3) player.position.x += 1.0f;
      if (scenario == 4 || scenario == 7) {
        const int x = (int)floorf(player.position.x);
        assert(world_set_block(&world, x, 410, 0, f % 2 ? BLOCK_TYPE_STONE : BLOCK_TYPE_SNOW));
      }
      if (scenario == 5) assert(world_set_block(&world, f * 16, 512 + f * 16, 0, BLOCK_TYPE_STONE));
      if (scenario == 6) renderer.viewDistance = 300.0f;
      if (scenario == 7 || scenario == 8) renderer.viewDistance = 25.0f;
      const double t = glfwGetTime();
      if (scenario == 7) {
        storage_reject = reject_mesh;
        assert(!application_stream_view(&rendererStreaming, &renderer, &world, &player, NULL));
        storage_reject = NULL;
      }
      assert(application_stream_view(&rendererStreaming, &renderer, &world, &player, NULL));
      /* Exercise retained draw-index capacity without camera-dependent culling. */
      const float mvp[16] = {0};
      assert(renderer_collect_draw_meshes(&renderer, player.position, renderer.viewDistance, mvp));
      times[f] = (glfwGetTime() - t) * 1000.0;
    }
    glFinish();
    size_t gpu = 0;
    for (size_t i = 0; i < renderer.meshCount; i++)
      for (int t = 0; t < BLOCK_TYPE_COUNT; t++) gpu += (size_t)renderer.meshes[i].vertexCount[t] * sizeof(SurfaceVertex);
    qsort(times, 32, sizeof(*times), compare);
    struct rusage usage;
    assert(getrusage(RUSAGE_SELF, &usage) == 0);
    for (int c = 0; c < STORAGE_CATEGORY_COUNT; c++) {
      const StorageStats *const s = storage_stats((StorageCategory)c);
      size_t used = s->live;
      if (c == STORAGE_VISIBLE) used = renderer.visibleMeshCount * sizeof(size_t);
      if (c == STORAGE_DRAW) used = renderer.drawMeshCount * sizeof(size_t);
      if (scenario == 1) assert(s->attempts == before[c].attempts &&
                               s->systemAllocations == before[c].systemAllocations);
      printf("%s,%s,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%.6f,%.6f,%.6f,%.6f,%ld,%zu\n",
             scenarios[scenario], categories[c], s->attempts - before[c].attempts,
             s->requested - before[c].requested, s->live, s->peakLive, s->capacity,
             s->peakCapacity, s->capacity - used, s->systemAllocations - before[c].systemAllocations,
             s->reuse - before[c].reuse, s->failures - before[c].failures,
             times[16], times[30], times[31], times[31], usage.ru_maxrss, gpu);
    }
  }
  renderer_destroy(&renderer);
  world_destroy(&world);
  for (int c = 0; c < STORAGE_CATEGORY_COUNT; c++) {
    assert(!storage_stats((StorageCategory)c)->live);
    assert(!storage_stats((StorageCategory)c)->capacity);
  }
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
