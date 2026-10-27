/* Fixed-seed travel benchmark with real GL resource retirement and no edits. */
#include "renderer_test.h"
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
         WORLD_SEED, a->world.blocksPerSide, (const char *)glGetString(GL_RENDERER));
  for (int sample = 0; sample < 5; sample++) {
    double start = glfwGetTime();
    for (int frame = 0; frame < 1000; frame++)
      assert(renderer_sync_view(&a->renderer, &a->world, &a->player));
    printf("SYNC stationary_sample=%d average_ms=%.6f\n", sample, glfwGetTime() - start);
  }
  double start = glfwGetTime();
  for (int frame = 0; frame < 120; frame++) {
    a->player.position.x = 0.5f + (float)frame;
    assert(renderer_sync_view(&a->renderer, &a->world, &a->player));
  }
  printf("SYNC walking_frames=120 stride=1 average_ms=%.6f\n",
         (glfwGetTime() - start)*1000/120);
  assert(glGetError() == GL_NO_ERROR);
}

/* Fixed simulation steps; render/swap submission is timed, GPU drain is outside
   every normal frame loop. --baseline retains the synchronous reference. */
static int capture_compare(const void *const a, const void *const b) {
  const double x = *(const double *)a, y = *(const double *)b;
  return (x > y) - (x < y);
}
enum { CAPTURE_FRAMES = 240 };
static void capture_frames(Application *const a, const int baseline) {
  const float radii[] = {25.0f, 100.0f, 300.0f};
  const char *const names[] = {"stationary", "walking", "running", "teleport",
                               "reverse", "radius", "edge"};
  printf("FRAMES mode=%s seed=%u compiler=%s vendor=%s GL=%s version=%s resolution=640x480 swap_interval=0\n",
         baseline ? "baseline" : "scheduled", WORLD_SEED, __VERSION__,
         (const char *)glGetString(GL_VENDOR), (const char *)glGetString(GL_RENDERER),
         (const char *)glGetString(GL_VERSION));
  puts("radius,scenario,sync_p50_ms,sync_p95_ms,sync_p99_ms,sync_max_ms,frame_p50_ms,frame_p95_ms,frame_p99_ms,frame_max_ms,materialize_ms,mesh_ms,upload_submission_ms,retire_ms,metadata_ms,operations,upload_bytes,peak_pending,peak_chunks,peak_sections,peak_gpu_bytes,atomic_max_ms,convergence_frames,peak_queue,peak_cpu_owned_bytes,materialized_chunks,section_builds,last_request_loading_frames,submission_p50_ms,submission_p95_ms,submission_p99_ms,submission_max_ms");
  for (size_t r = 0; r < sizeof(radii)/sizeof(*radii); r++) {
    a->renderer.viewDistance = radii[r];
    a->player.position = (Vec3){-16.5f, 410.0f, -16.5f};
    a->player.velocityY = 0.0f;
    a->player.grounded = 0;
    assert(renderer_sync_view(&a->renderer, &a->world, &a->player));
    glFinish();
    for (int scenario = 0; scenario < 7; scenario++) {
      double sync[CAPTURE_FRAMES], frame[CAPTURE_FRAMES], submission[CAPTURE_FRAMES];
      FrameWork total = {0};
      a->renderer.peakPending = 0;
      size_t peakPending = 0, peakChunks = 0, peakSections = 0, peakGpu = 0;
      double atomicMax = 0;
      size_t peakQueue = 0, peakCpu = 0, loadingFrames = 0;
      int waiting = 0, requestFrame = 0;
      for (int f = 0; f < CAPTURE_FRAMES; f++) {
        a->performance.frame = (FrameWork){0};
        const double frameStart = glfwGetTime();
        if (((scenario == 3 || scenario == 6) && f == 0) || (scenario == 5 && f == 120)) {
          waiting = 1;
          requestFrame = f;
          loadingFrames = 0;
        }
        if (scenario == 3 && f == 0) a->player.position = (Vec3){-1200.5f, 410.0f, -900.5f};
        if (scenario == 5) a->renderer.viewDistance = f < 120 ? 25.0f : radii[r];
        if (scenario == 6 && f == 0) a->player.position = (Vec3){2300.5f, 410.0f, -2300.5f};
        if ((scenario == 3 || scenario == 6) && f == 0) {
          a->player.position.y = world_surface_height(&a->world, a->player.position.x, a->player.position.z) + 8.0f;
          a->player.velocityY = 0.0f;
          a->player.grounded = 0;
        }
        update_player(&a->player, (PlayerInput){
            .strafe = scenario == 4 ? -1.0f : (scenario == 1 || scenario == 2) ? 1.0f : 0.0f,
            .run = scenario == 2}, 1.0f / 60.0f, &a->world);
        const double start = glfwGetTime();
        const StreamResult result = baseline ?
            renderer_sync_view_work(&a->renderer, &a->world, &a->player, NULL) :
            renderer_step_view(&a->renderer, &a->world, &a->player);
        assert(result != STREAM_FAILED);
        if (waiting && result == STREAM_COMPLETE) {
          loadingFrames = (size_t)(f + 1 - requestFrame);
          waiting = 0;
        }
        sync[f] = glfwGetTime() - start;
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        const double submissionStart = glfwGetTime();
        render_world(&a->renderer, &a->player);
        submission[f] = glfwGetTime() - submissionStart;
        glfwSwapBuffers(a->window);
        frame[f] = glfwGetTime() - frameStart;
        const FrameWork *const work = &a->performance.frame;
        total.materialize += work->materialize;
        total.mesh += work->mesh;
        total.upload += work->upload;
        total.uploadBytes += work->uploadBytes;
        total.retirement += work->retirement;
        total.metadata += work->metadata;
        total.operations += work->operations;
        total.rebuilds += work->rebuilds;
        total.materializedChunks += work->materializedChunks;
        if (work->atomicMax > atomicMax) atomicMax = work->atomicMax;
        if (a->renderer.pendingWork > peakPending) peakPending = a->renderer.pendingWork;
        performance_snapshot(a);
        if (a->renderer.peakPending > peakQueue) peakQueue = a->renderer.peakPending;
        const size_t bound = (size_t)ceilf(2.0f * a->renderer.viewDistance / SECTION_SIZE) + 3;
        assert(a->residentChunks <= bound * bound);
        assert(a->world.sectionCount <= bound * bound * WORLD_SECTION_COUNT);
        assert(a->renderer.pendingCount <= STREAM_TASK_CAPACITY);
        if (scenario == 0) assert(!work->operations && !work->rebuilds && !work->uploadCalls);
        const size_t owned = sizeof(a->world) + a->world.chunkCount * sizeof(Chunk) +
            (a->world.edits ? WORLD_EDIT_CAPACITY * sizeof(VoxelEdit) : 0) + a->residentVoxelBytes +
            sizeof(a->renderer) + a->renderer.meshCount * sizeof(SectionGpuMesh) +
            (a->renderer.visibleMeshCapacity + a->renderer.drawMeshCapacity) * sizeof(size_t);
        if (owned > peakCpu) peakCpu = owned;
        if (a->residentChunks > peakChunks) peakChunks = a->residentChunks;
        if (a->world.sectionCount > peakSections) peakSections = a->world.sectionCount;
        if (a->gpuBytes > peakGpu) peakGpu = a->gpuBytes;
      }
      size_t convergence = 0;
      if (!baseline) {
        StreamResult result;
        do {
          a->performance.frame = (FrameWork){0};
          result = renderer_step_view(&a->renderer, &a->world, &a->player);
          assert(result != STREAM_FAILED);
          if (a->performance.frame.atomicMax > atomicMax) atomicMax = a->performance.frame.atomicMax;
          if (result != STREAM_COMPLETE) assert(++convergence < 100000);
          else if (waiting) loadingFrames = CAPTURE_FRAMES + convergence + 1 - (size_t)requestFrame;
        } while (result != STREAM_COMPLETE);
      }
      glFinish();
      assert(glGetError() == GL_NO_ERROR);
      qsort(sync, CAPTURE_FRAMES, sizeof(*sync), capture_compare);
      qsort(frame, CAPTURE_FRAMES, sizeof(*frame), capture_compare);
      qsort(submission, CAPTURE_FRAMES, sizeof(*submission), capture_compare);
      printf("%.0f,%s,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%zu,%zu,%zu,%zu,%zu,%zu,%.3f,%zu,%zu,%zu,%zu,%zu,%zu,%.3f,%.3f,%.3f,%.3f\n",
             radii[r], names[scenario], sync[119]*1000, sync[227]*1000, sync[237]*1000, sync[239]*1000,
             frame[119]*1000, frame[227]*1000, frame[237]*1000, frame[239]*1000,
             total.materialize*1000, total.mesh*1000, total.upload*1000,
             total.retirement*1000, total.metadata*1000, total.operations, total.uploadBytes, peakPending, peakChunks, peakSections, peakGpu, atomicMax*1000, convergence, peakQueue, peakCpu, total.materializedChunks, total.rebuilds, loadingFrames,
             submission[119]*1000, submission[227]*1000,
             submission[237]*1000, submission[239]*1000);
      fflush(stdout);
    }
  }
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
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
  glViewport(0, 0, 640, 480);
  static Application a;
  a.window = window;
  assert(create_terrain_streamed(&a.world, PLATFORM_BLOCKS_PER_SIDE, CENTER, 1, WORLD_SEED));
  assert(renderer_init(&a.renderer));
  a.performance.enabled = 1;
  a.renderer.performance = &a.performance;
  a.renderer.viewDistance = 25;
  a.renderer.framebufferWidth = 640;
  a.renderer.framebufferHeight = 480;
  a.player = (Player){.eyeHeight = 1,
      .camera = {.front = {0, -0.8944272f, -0.4472136f}, .up = {0, 1, 0}}};
  if (argc == 2 && (!strcmp(argv[1], "--frames") || !strcmp(argv[1], "--baseline"))) {
    capture_frames(&a, !strcmp(argv[1], "--baseline"));
    goto cleanup;
  }
  if (argc == 2 && strcmp(argv[1], "--sync") == 0) {
    capture_sync_cost(&a);
    goto cleanup;
  }
  /* ceil(2*radius/chunkSpan)+3 columns per axis, including padding. */
  const size_t bound = (size_t)ceil(2 * a.renderer.viewDistance / SECTION_SIZE) + 3;
  size_t peakChunks = 0, peakSections = 0, peakGpu = 0, evictions = 0, retirements = 0;
  double total = 0, maximum = 0;
  printf("STREAM seed=%u side=%d distance=25 steps=64 stride=96 chunk_bound=%zu GL=%s\n",
         WORLD_SEED, a.world.blocksPerSide, bound * bound, (const char *)glGetString(GL_RENDERER));
  puts("step,x,sync_ms,materialize_ms,mesh_ms,upload_ms,gpu_drain_ms,chunks,sections,cpu_owned_bytes,rss_peak_kib,gpu_requested_bytes,evicted_chunks,retired_meshes,retired_bytes");
  for (int step = 0; step <= 64; step++) {
    int travel = step < 32 ? step : 63 - step;
    a.player.position.x = step == 64 ? 10000 : -1487.5f + (float)travel * 96.0f;
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
        assert(fabsf((float)chunk->x - cx) <= ceilf(25.0f / SECTION_SIZE) + 1);
        assert(fabsf((float)chunk->z - cz) <= ceilf(25.0f / SECTION_SIZE) + 1);
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
        (a.world.edits ? WORLD_EDIT_CAPACITY * sizeof(VoxelEdit) : 0) +
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
