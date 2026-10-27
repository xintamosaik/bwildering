/* Comparison implementations live only in this opt-in experiment.
   Record exact submitted ranges only during untimed correctness snapshots. */
#include <GL/glew.h>
void __real_glDrawArrays(GLenum mode, GLint first, GLsizei count);
#include <assert.h>
typedef struct { GLint vao, first, count; } SubmittedRange;
static SubmittedRange ranges[16384];
static size_t rangeCount;
static int recordRanges;
void __wrap_glDrawArrays(const GLenum mode, const GLint first, const GLsizei count) {
  if (recordRanges) {
    assert(rangeCount < sizeof(ranges) / sizeof(*ranges));
    GLint vao;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    ranges[rangeCount++] = (SubmittedRange){vao, first, count};
  }
  __real_glDrawArrays(mode, first, count);
}
#include "renderer_test.h"
#include <assert.h>

static void render_reference(Renderer *renderer, const Player *player) {
  if (renderer->framebufferWidth <= 0 || renderer->framebufferHeight <= 0)
    return;

  glUseProgram(renderer->program);

  mat4 proj, view, mvp;
  const float aspectRatio =
      (float)renderer->framebufferWidth / (float)renderer->framebufferHeight;
  const float viewDistance = renderer->viewDistance > 0 ? renderer->viewDistance : DEFAULT_VIEW_DISTANCE;
  mat4_perspective(75.0f * (3.14159f / 180.0f), aspectRatio, 0.05f, viewDistance, proj);
  const Vec3 cameraPosition = player_eye_position(player);
  const Vec3 target = vec3_add(cameraPosition, player->camera.front);
  mat4_lookat(cameraPosition, target, player->camera.up, view);
  mat4_multiply(proj, view, mvp);

  glUniformMatrix4fv(renderer->mvpLocation, 1, GL_FALSE, (float *)mvp);
  for (int type = 0; type < BLOCK_TYPE_COUNT; type++) {
    Vec3 color;
    if (!block_type_color((BlockType)type, &color))
      continue;
    glUniform3f(renderer->colorLocation, color.x, color.y, color.z);
    if (renderer->performance && renderer->performance->enabled) renderer->performance->frame.materialChanges++;
    const size_t count = renderer->streamed ? renderer->visibleMeshCount : renderer->meshCount;
    for (size_t n = 0; n < count; n++) {
      const size_t i = renderer->streamed ? renderer->visibleMeshes[n] : n;
      const SectionGpuMesh *mesh = &renderer->meshes[i];
      if (!mesh->vertexCount[type]) continue;
      if (renderer->performance && renderer->performance->enabled) renderer->performance->frame.distanceTests++;
      if (!section_within_view_distance(mesh, cameraPosition, viewDistance)) continue;
      if (renderer->performance && renderer->performance->enabled) renderer->performance->frame.frustumTests++;
      if (!section_in_frustum(mesh, mvp)) continue;
      glBindVertexArray(mesh->vao);
      glDrawArrays(GL_TRIANGLES, mesh->firstVertex[type], mesh->vertexCount[type]);
      if (renderer->performance && renderer->performance->enabled) {
        renderer->performance->frame.draws++;
        renderer->performance->frame.triangles += (size_t)mesh->vertexCount[type] / 3;
      }
    }
  }
}

static void render_section_first(Renderer *renderer, const Player *player) {
  if (renderer->framebufferWidth <= 0 || renderer->framebufferHeight <= 0)
    return;

  glUseProgram(renderer->program);

  mat4 proj, view, mvp;
  const float aspectRatio =
      (float)renderer->framebufferWidth / (float)renderer->framebufferHeight;
  const float viewDistance = renderer->viewDistance > 0 ? renderer->viewDistance : DEFAULT_VIEW_DISTANCE;
  mat4_perspective(75.0f * (3.14159f / 180.0f), aspectRatio, 0.05f, viewDistance, proj);
  const Vec3 cameraPosition = player_eye_position(player);
  const Vec3 target = vec3_add(cameraPosition, player->camera.front);
  mat4_lookat(cameraPosition, target, player->camera.up, view);
  mat4_multiply(proj, view, mvp);

  double start = perf_clock(renderer->performance);
  assert(renderer_collect_draw_meshes(renderer, cameraPosition, viewDistance, mvp));
  if (renderer->performance && renderer->performance->enabled)
    renderer->performance->frame.visibility += perf_clock(renderer->performance) - start;
  glUniformMatrix4fv(renderer->mvpLocation, 1, GL_FALSE, (float *)mvp);
  int last = -1;
  for (size_t n = 0; n < renderer->drawMeshCount; n++) {
    const SectionGpuMesh *mesh = &renderer->meshes[renderer->drawMeshes[n]];
    glBindVertexArray(mesh->vao);
    for (int type = 0; type < BLOCK_TYPE_COUNT; type++) {
      Vec3 color;
      if (!mesh->vertexCount[type] || !block_type_color((BlockType)type, &color)) continue;
      if (last != type) {
        glUniform3f(renderer->colorLocation, color.x, color.y, color.z);
        last = type;
        if (renderer->performance && renderer->performance->enabled)
          renderer->performance->frame.materialChanges++;
      }
      glDrawArrays(GL_TRIANGLES, mesh->firstVertex[type], mesh->vertexCount[type]);
      if (renderer->performance && renderer->performance->enabled) {
        renderer->performance->frame.draws++;
        renderer->performance->frame.triangles += (size_t)mesh->vertexCount[type] / 3;
      }
    }
  }
}

static int compare_range(const void *a, const void *b) {
  const SubmittedRange *x = a, *y = b;
  if (x->vao != y->vao) return (x->vao > y->vao) - (x->vao < y->vao);
  if (x->first != y->first) return (x->first > y->first) - (x->first < y->first);
  return (x->count > y->count) - (x->count < y->count);
}

typedef void (*Draw)(Renderer *, const Player *);
static Draw orders[] = {render_reference, render_world, render_section_first};
static const char *names[] = {"reference", "material", "section"};

typedef struct {
  Performance wall, submission;
  FrameWork totals;
} Result;

static void compare(GLFWwindow *window, Renderer *r, const Player *player, const char *scene) {
  Performance p = {.enabled = 1};
  r->performance = &p;
  Result results[3] = {0};
  float depth[640 * 480], reference[640 * 480];
  size_t draws = 0, triangles = 0;
  SubmittedRange referenceRanges[16384];
  for (int order = 0; order < 3; order++) {
    p.frame = (FrameWork){0};
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    double start = glfwGetTime();
    orders[order](r, player);
    double cold = glfwGetTime() - start;
    double coldList = p.frame.visibility;
    /* Keep range recording and depth readback out of cold/steady timings. */
    p.frame = (FrameWork){0};
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    rangeCount = 0; recordRanges = 1;
    orders[order](r, player);
    recordRanges = 0;
    qsort(ranges, rangeCount, sizeof(*ranges), compare_range);
    glReadPixels(0, 0, 640, 480, GL_DEPTH_COMPONENT, GL_FLOAT, depth);
    if (!order) {
      memcpy(reference, depth, sizeof(depth));
      draws = p.frame.draws; triangles = p.frame.triangles;
      assert(rangeCount == draws);
      memcpy(referenceRanges, ranges, rangeCount * sizeof(*ranges));
      size_t covered = 0;
      for (size_t i = 0; i < 640 * 480; i++) covered += depth[i] < 1;
      assert(covered);
    } else {
      assert(draws == p.frame.draws && triangles == p.frame.triangles);
      assert(rangeCount == draws);
      for (size_t i = 0; i < rangeCount; i++)
        assert(!compare_range(&ranges[i], &referenceRanges[i]));
      for (size_t i = 0; i < 640 * 480; i++) assert(fabsf(depth[i] - reference[i]) < 0.00001f);
    }
    printf("COLD scene=%s distance=%.0f order=%s render_ms=%.3f list_ms=%.3f index_bytes=%zu\n",
           scene, r->viewDistance, names[order], cold*1000, coldList*1000,
           r->drawMeshCapacity * sizeof(size_t));
    glfwSwapBuffers(window);
    results[order].wall.enabled = results[order].submission.enabled = 1;
  }
  /* Rotate the first order each round to reduce sequential-run bias. */
  for (int round = 0; round < 6; round++)
    for (int slot = 0; slot < 3; slot++) {
      int order = (round + slot) % 3;
      Result *result = &results[order];
      for (int frame = 0; frame < PERF_SAMPLES / 6; frame++) {
        p.frame = (FrameWork){0};
        double start = glfwGetTime();
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        double submit = glfwGetTime();
        orders[order](r, player);
        result->submission.frame.raw = glfwGetTime() - submit;
        glfwSwapBuffers(window);
        result->wall.frame.raw = glfwGetTime() - start;
        assert(draws == p.frame.draws && triangles == p.frame.triangles);
        assert(glGetError() == GL_NO_ERROR);
        perf_record(&result->wall);
        perf_record(&result->submission);
#define ADD(field) result->totals.field += p.frame.field
        ADD(visibility); ADD(distanceTests); ADD(frustumTests); ADD(materialChanges);
#undef ADD
      }
    }
  for (int order = 0; order < 3; order++) {
    Result *result = &results[order];
    perf_finish_window(&result->wall); perf_finish_window(&result->submission);
    printf("VIS scene=%s distance=%.0f order=%s frames=%zu frame_ms=%.4f/%.4f/%.4f "
           "submission_ms=%.4f/%.4f/%.4f list_ms=%.4f distance_tests=%zu frustum_tests=%zu "
           "material_changes=%zu draws=%zu triangles=%zu index_bytes=%zu\n", scene,
           r->viewDistance, names[order], result->wall.count,
           result->wall.average*1000, result->wall.p95*1000, result->wall.maximum*1000,
           result->submission.average*1000, result->submission.p95*1000, result->submission.maximum*1000,
           result->totals.visibility*1000/PERF_SAMPLES, result->totals.distanceTests/PERF_SAMPLES,
           result->totals.frustumTests/PERF_SAMPLES, result->totals.materialChanges/PERF_SAMPLES,
           draws, triangles, r->drawMeshCapacity*sizeof(size_t));
  }
  r->performance = NULL;
}

int main(void) {
  glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
  assert(glfwInit());
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  GLFWwindow *window = glfwCreateWindow(640, 480, "Visibility comparison", NULL, NULL);
  assert(window);
  glfwMakeContextCurrent(window);
  assert(glewInit() == GLEW_OK);
  while (glGetError() != GL_NO_ERROR) {}
  glfwSwapInterval(0);
  glEnable(GL_DEPTH_TEST); glEnable(GL_CULL_FACE);
  glViewport(0, 0, 640, 480);
  printf("VIS compiler=%s GL=%s version=%s seed=%u resolution=640x480 swap_interval=0 overlay=0\n",
         __VERSION__, (const char *)glGetString(GL_RENDERER), (const char *)glGetString(GL_VERSION), WORLD_SEED);
  for (int streamed = 0; streamed < 2; streamed++) {
    World world = {0}; Renderer r = {0};
    assert(renderer_init(&r));
    r.framebufferWidth = 640; r.framebufferHeight = 480;
    if (streamed) assert(create_terrain_streamed(&world, 4608, (Vec3){0}, 1, WORLD_SEED));
    else assert(create_terrain(&world, 192, (Vec3){0}, 1, WORLD_SEED));
    Player player = {.position = {0.5f, 0, 0.5f}, .eyeHeight = 1,
        .camera = {.front = {0, -0.4472136f, -0.8944272f}, .up = {0, 1, 0}}};
    player.position.y = world_surface_height(&world, player.position.x, player.position.z) + 8;
    const float distances[] = {25, 100, 300};
    for (size_t i = 0; i < 3; i++) {
      r.viewDistance = distances[i];
      assert(renderer_sync_view(&r, &world, &player));
      compare(window, &r, &player, streamed ? "streamed-4608" : "ordinary-192");
    }
    renderer_destroy(&r); world_destroy(&world);
  }
  glfwDestroyWindow(window); glfwTerminate();
}
