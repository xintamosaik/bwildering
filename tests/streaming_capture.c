/* Fixed-seed travel benchmark with real GL resource retirement and no edits. */
#define main bwildering_application_main
#include "../main.c"
#undef main
#include <assert.h>
#include <sys/resource.h>

/* Warm frame-path comparison: no drawing, instrumentation or GPU drains in the
   timed loops. Each stationary sample averages 1,000 synchronization calls. */
static void capture_sync_cost(Application *a) {
  a->performance.enabled = 0;
  a->renderer.viewDistance = 100;
  a->player.position = (Vec3){0.5f, 410, 0.5f};
  assert(renderer_sync_view(&a->renderer, &a->world, &a->player));
  glFinish();
  printf("SYNC seed=%u side=%d distance=100 samples=5 frames=1000 GL=%s\n",
         WORLD_SEED, a->world.blocksPerSide, glGetString(GL_RENDERER));
  for (int sample = 0; sample < 5; sample++) {
    double start = glfwGetTime();
    for (int frame = 0; frame < 1000; frame++)
      assert(renderer_sync_view(&a->renderer, &a->world, &a->player));
    printf("SYNC stationary_sample=%d average_ms=%.6f\n", sample, glfwGetTime() - start);
  }
  double start = glfwGetTime();
  for (int frame = 0; frame < 120; frame++) {
    a->player.position.x = 0.5f + frame;
    assert(renderer_sync_view(&a->renderer, &a->world, &a->player));
  }
  printf("SYNC walking_frames=120 stride=1 average_ms=%.6f\n",
         (glfwGetTime() - start)*1000/120);
  assert(glGetError() == GL_NO_ERROR);
}

int main(int argc, char **argv) {
  glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
  assert(glfwInit());
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  GLFWwindow *window = glfwCreateWindow(640, 480, "Streaming capture", NULL, NULL);
  assert(window);
  glfwMakeContextCurrent(window);
  glewExperimental = GL_TRUE;
  assert(glewInit() == GLEW_OK);
  while (glGetError() != GL_NO_ERROR) {}
  glfwSwapInterval(0);
  Application a = {0};
  assert(create_terrain_streamed(&a.world, PLATFORM_BLOCKS_PER_SIDE, CENTER, 1, WORLD_SEED));
  assert(renderer_init(&a.renderer));
  a.performance.enabled = 1;
  a.renderer.performance = &a.performance;
  a.renderer.viewDistance = 25;
  a.renderer.framebufferWidth = 640;
  a.renderer.framebufferHeight = 480;
  a.player = (Player){.eyeHeight = 1,
      .camera = {.front = {0, -0.8944272f, -0.4472136f}, .up = {0, 1, 0}}};
  if (argc == 2 && strcmp(argv[1], "--sync") == 0) {
    capture_sync_cost(&a);
    goto cleanup;
  }
  /* ceil(2*radius/chunkSpan)+3 columns per axis, including padding. */
  const size_t bound = (size_t)ceil(2 * a.renderer.viewDistance / SECTION_SIZE) + 3;
  size_t peakChunks = 0, peakSections = 0, peakGpu = 0, evictions = 0, retirements = 0;
  double total = 0, maximum = 0;
  printf("STREAM seed=%u side=%d distance=25 steps=64 stride=96 chunk_bound=%zu GL=%s\n",
         WORLD_SEED, a.world.blocksPerSide, bound * bound, glGetString(GL_RENDERER));
  puts("step,x,sync_ms,materialize_ms,mesh_ms,upload_ms,gpu_drain_ms,chunks,sections,cpu_owned_bytes,rss_peak_kib,gpu_requested_bytes,evicted_chunks,retired_meshes,retired_bytes");
  for (int step = 0; step <= 64; step++) {
    int travel = step < 32 ? step : 63 - step;
    a.player.position.x = step == 64 ? 10000 : -1487.5f + travel * 96;
    a.player.position.z = 0.5f;
    a.player.position.y = world_surface_height(&a.world, a.player.position.x, 0.5f) + 8;
    a.performance.frame = (FrameWork){0};
    double start = glfwGetTime();
    assert(renderer_sync_view(&a.renderer, &a.world, &a.player));
    double sync = glfwGetTime() - start;
    start = glfwGetTime();
    glFinish();
    double drain = glfwGetTime() - start;
    assert(glGetError() == GL_NO_ERROR);
    performance_snapshot(&a);
    assert(!a.editedChunks && a.residentChunks <= bound * bound);
    assert(a.world.sectionCount <= bound * bound * WORLD_SECTION_COUNT);
    assert(a.renderer.meshCount == a.world.sectionCount);
    for (size_t c = 0; c < a.world.chunkCount; c++) {
      const Chunk *chunk = &a.world.chunks[c];
      if (chunk->terrainReady) {
        float cx = floorf(a.player.position.x / SECTION_SIZE);
        float cz = floorf(a.player.position.z / SECTION_SIZE);
        assert(fabsf(chunk->x - cx) <= ceilf(25.0f / SECTION_SIZE) + 1);
        assert(fabsf(chunk->z - cz) <= ceilf(25.0f / SECTION_SIZE) + 1);
      }
    }
    if (a.residentChunks > peakChunks) peakChunks = a.residentChunks;
    if (a.world.sectionCount > peakSections) peakSections = a.world.sectionCount;
    if (a.gpuBytes > peakGpu) peakGpu = a.gpuBytes;
    evictions += a.performance.frame.evictedChunks;
    retirements += a.performance.frame.retiredMeshes;
    total += sync;
    if (sync > maximum) maximum = sync;
    struct rusage usage;
    assert(getrusage(RUSAGE_SELF, &usage) == 0);
    size_t owned = sizeof(a.world) + a.world.chunkCount * sizeof(Chunk) +
        a.residentVoxelBytes + sizeof(a.renderer) +
        a.renderer.meshCount * sizeof(SectionGpuMesh) +
        a.renderer.visibleMeshCapacity * sizeof(size_t) + a.renderer.drawMeshCapacity * sizeof(size_t);
    FrameWork *f = &a.performance.frame;
    printf("%d,%.1f,%.3f,%.3f,%.3f,%.3f,%.3f,%zu,%zu,%zu,%ld,%zu,%zu,%zu,%zu\n",
           step, a.player.position.x, sync*1000, f->materialize*1000, f->mesh*1000,
           f->upload*1000, drain*1000, a.residentChunks, a.world.sectionCount, owned,
           usage.ru_maxrss, a.gpuBytes, f->evictedChunks, f->retiredMeshes, f->retiredBytes);
  }
  assert(!a.residentChunks && !a.world.sectionCount && !a.gpuBytes && !a.renderer.meshCount);
  assert(evictions && retirements);
  printf("STREAM peak_chunks=%zu peak_sections=%zu peak_gpu_bytes=%zu evictions=%zu retired_meshes=%zu sync_avg_ms=%.3f sync_max_ms=%.3f\n",
         peakChunks, peakSections, peakGpu, evictions, retirements, total*1000/65, maximum*1000);
cleanup:
  renderer_destroy(&a.renderer);
  world_destroy(&a.world);
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
