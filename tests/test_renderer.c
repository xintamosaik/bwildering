/* Exercise the private renderer in a real GL context without adding test hooks
   or development controls to the game. */
#include <stdlib.h>
#include <assert.h>
#include "../application_internal.h"
void __real_glDrawArrays(GLenum mode, GLint first, GLsizei count);
#include "../storage.h"
void __wrap_glDrawArrays(GLenum mode, GLint first, GLsizei count);
/* Guard the active-frame path against synchronous window-system size queries. */
static int windowSizeQueries, framebufferSizeQueries, viewportUpdates;
void __real_glfwGetWindowSize(GLFWwindow *window, int *width, int *height);
void __real_glfwGetFramebufferSize(GLFWwindow *window, int *width, int *height);
void __real_glViewport(GLint x, GLint y, GLsizei width, GLsizei height);
void __wrap_glfwGetWindowSize(GLFWwindow *const window, int *const width, int *const height) {
  windowSizeQueries++;
  __real_glfwGetWindowSize(window, width, height);
}
void __wrap_glfwGetFramebufferSize(GLFWwindow *const window, int *const width, int *const height) {
  framebufferSizeQueries++;
  __real_glfwGetFramebufferSize(window, width, height);
}
void __wrap_glViewport(const GLint x, const GLint y, const GLsizei width, const GLsizei height) {
  viewportUpdates++;
  __real_glViewport(x, y, width, height);
}
static int scratch_reservations_before_failure = -1;
static int draw_allocations_before_failure = -1;
static int metadata_allocations_before_failure = -1;
static int visible_allocations_before_failure = -1;
static int reject_storage(const StorageCategory category) {
  int *const remaining = category == STORAGE_DRAW ? &draw_allocations_before_failure :
      category == STORAGE_RECORD ? &metadata_allocations_before_failure :
      category == STORAGE_VISIBLE ? &visible_allocations_before_failure :
      category == STORAGE_MESH ? &scratch_reservations_before_failure : NULL;
  if (!remaining || *remaining < 0) return 0;
  if (*remaining == 0) return 1;
  (*remaining)--;
  return 0;
}
#include "renderer_test.h"

/* Record real submissions only while a verification capture is active. */
typedef struct { GLuint vao; GLint first; GLsizei count; } AuditRange;
enum { AUDIT_RANGE_CAPACITY = 32768, AUDIT_WIDTH = 320, AUDIT_HEIGHT = 240 };
static AuditRange auditRanges[AUDIT_RANGE_CAPACITY];
static size_t auditRangeCount;
static const Renderer *auditRenderer;

void __wrap_glDrawArrays(const GLenum mode, const GLint first, const GLsizei count) {
  if (auditRenderer) {
    GLint vao = 0, vbo = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &vbo);
    assert(mode == GL_TRIANGLES && first >= 0 && count > 0 && count % 3 == 0);
    assert(glIsVertexArray((GLuint)vao) && glIsBuffer((GLuint)vbo));
    size_t owners = 0, matches = 0;
    for (size_t i = 0; i < auditRenderer->meshCount; i++) {
      const SectionGpuMesh *const mesh = &auditRenderer->meshes[i];
      if (mesh->vao != (GLuint)vao) continue;
      owners++;
      assert(mesh->vbo == (GLuint)vbo);
      for (int t = 0; t < BLOCK_TYPE_COUNT; t++)
        matches += mesh->firstVertex[t] == first && mesh->vertexCount[t] == count;
    }
    assert(owners == 1 && matches == 1);
    assert(auditRangeCount < AUDIT_RANGE_CAPACITY);
    auditRanges[auditRangeCount++] = (AuditRange){(GLuint)vao, first, count};
  }
  __real_glDrawArrays(mode, first, count);
}

static PFNGLBUFFERDATAPROC real_buffer_upload;
static int uploads_before_failure;

static void APIENTRY fail_buffer_upload(GLenum target, GLsizeiptr size, const void *data,
                                        GLenum usage) {
  if (uploads_before_failure-- == 0)
    glBindBuffer((GLenum)0, 0); /* Deliberate GL_INVALID_ENUM. */
  else
    real_buffer_upload(target, size, data, usage);
}

static SectionGpuMesh *gpu_section(Renderer *renderer, int x, int y, int z) {
  for (size_t i = 0; i < renderer->meshCount; i++) {
    SectionGpuMesh *mesh = &renderer->meshes[i];
    if (mesh->chunkX == x && mesh->sectionY == y && mesh->chunkZ == z)
      return mesh;
  }
  return NULL;
}

static int vertex_count(const SectionGpuMesh *mesh) {
  int n = 0;
  for (int t = 0; t < BLOCK_TYPE_COUNT; t++)
    n += mesh->vertexCount[t];
  return n;
}

/* Compare actual driver-owned bytes and draw ranges, not just vertex totals.
   CPU snapshots are independently owned and survive GPU retirement. */
static void assert_gpu_geometry(const SectionGpuMesh *gpu, const SectionMesh *cpu) {
  assert(gpu && (size_t)vertex_count(gpu) == cpu->vertexCount);
  for (int t = 0; t < BLOCK_TYPE_COUNT; t++) {
    assert((size_t)gpu->firstVertex[t] == cpu->firstVertex[t]);
    assert((size_t)gpu->vertexCount[t] == cpu->typeVertexCount[t]);
  }
  if (cpu->vertexCount) {
    assert(glIsBuffer(gpu->vbo) && glIsVertexArray(gpu->vao));
    size_t bytes = cpu->vertexCount * sizeof(*cpu->vertices);
    SurfaceVertex *vertices = malloc(bytes);
    assert(vertices);
    glBindBuffer(GL_ARRAY_BUFFER, gpu->vbo);
    GLint size = 0;
    glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &size);
    assert((size_t)size == bytes);
    glGetBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)bytes, vertices);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    for (size_t v = 0; v < cpu->vertexCount; v++) {
      assert(vertices[v].x == cpu->vertices[v].x);
      assert(vertices[v].y == cpu->vertices[v].y);
      assert(vertices[v].z == cpu->vertices[v].z);
    }
    free(vertices);
  } else {
    assert(!gpu->vao && !gpu->vbo);
  }
  assert(glGetError() == GL_NO_ERROR);
}

static void assert_empty_gpu_world(Renderer *renderer) {
  for (size_t i = 0; i < renderer->meshCount; i++) {
    assert(!renderer->meshes[i].vao && !renderer->meshes[i].vbo);
    assert(vertex_count(&renderer->meshes[i]) == 0);
  }
  Player player = {.camera = {.front = {0, 0, -1}, .up = {0, 1, 0}}};
  GLuint query, primitives;
  glGenQueries(1, &query);
  glBeginQuery(GL_PRIMITIVES_GENERATED, query);
  render_world(renderer, &player);
  glEndQuery(GL_PRIMITIVES_GENERATED);
  glGetQueryObjectuiv(query, GL_QUERY_RESULT, &primitives);
  assert(primitives == 0);
  glDeleteQueries(1, &query);
  assert(glGetError() == GL_NO_ERROR);
}

static void glfw_test_error(int code, const char *message) {
  fprintf(stderr, "GLFW %d: %s\n", code, message);
}

typedef struct {
  float *depth;
  size_t count, covered;
} DepthFrame;

/* Depth distinguishes drawn geometry from any clear colour or palette. */
static DepthFrame capture_world(Renderer *renderer, const Player *player) {
  int width = renderer->framebufferWidth, height = renderer->framebufferHeight;
  assert(width > 0 && height > 0);
  DepthFrame frame = {.count = (size_t)width * height};
  frame.depth = malloc(frame.count * sizeof(*frame.depth));
  assert(frame.depth);
  glViewport(0, 0, width, height);
  glEnable(GL_DEPTH_TEST);
  glClearDepth(1);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  render_world(renderer, player);
  glReadPixels(0, 0, width, height, GL_DEPTH_COMPONENT, GL_FLOAT, frame.depth);
  for (size_t i = 0; i < frame.count; i++) {
    assert(isfinite(frame.depth[i]) && frame.depth[i] >= 0 && frame.depth[i] <= 1);
    frame.covered += frame.depth[i] < 1;
  }
  assert(glGetError() == GL_NO_ERROR);
  return frame;
}

static void assert_terrain_visible(const DepthFrame *frame) {
  assert(frame->covered > frame->count / 100);
}

static void assert_view_changed(const DepthFrame *before, const DepthFrame *after) {
  assert(before->count == after->count);
  size_t changed = 0;
  for (size_t i = 0; i < before->count; i++)
    changed += fabsf(before->depth[i] - after->depth[i]) > 0.00001f;
  assert(changed > before->count / 1000);
}

static void test_frustum(void) {
  mat4 identity;
  mat4_identity(identity);
  SectionGpuMesh mesh = {.boundsMin = {-0.5f, -0.5f, -0.5f},
                         .boundsMax = {0.5f, 0.5f, 0.5f}};
  assert(section_in_frustum(&mesh, identity));
  const Vec3 outside[6] = {{-3, 0, 0}, {3, 0, 0},  {0, -3, 0},
                           {0, 3, 0},  {0, 0, -3}, {0, 0, 3}};
  for (int i = 0; i < 6; i++) {
    mesh.boundsMin = vec3_sub(outside[i], (Vec3){0.5f, 0.5f, 0.5f});
    mesh.boundsMax = vec3_add(outside[i], (Vec3){0.5f, 0.5f, 0.5f});
    assert(!section_in_frustum(&mesh, identity));
  }
  mesh.boundsMin = (Vec3){0.5f, 0.5f, 0.5f};
  mesh.boundsMax = (Vec3){2, 2, 2};
  assert(section_in_frustum(&mesh, identity));
  mesh.boundsMin = (Vec3){-1, -1, 1};
  mesh.boundsMax = (Vec3){1, 1, 2};
  assert(section_in_frustum(&mesh, identity)); /* Touching the far plane. */
  mesh.boundsMin.z = 1.001f;
  assert(!section_in_frustum(&mesh, identity));
  mesh.boundsMin = (Vec3){25, 0, 0};
  mesh.boundsMax = (Vec3){26, 1, 1};
  assert(section_within_view_distance(&mesh, (Vec3){0}, 25));
  assert(!section_within_view_distance(&mesh, (Vec3){-0.001f, 0, 0}, 25));
}

/* Submit every owned mesh without residency, distance or frustum predicates.
   Hardware clipping and depth testing provide the independent reference. */
static void render_unculled(const Renderer *const renderer, const Player *const player) {
  mat4 projection, view, mvp;
  mat4_perspective(75.0f * (3.14159f / 180.0f),
                  (float)AUDIT_WIDTH / (float)AUDIT_HEIGHT, 0.05f,
                  renderer->viewDistance, projection);
  const Vec3 eye = player_eye_position(player);
  mat4_lookat(eye, vec3_add(eye, player->camera.front), player->camera.up, view);
  mat4_multiply(projection, view, mvp);
  glUseProgram(renderer->program);
  glUniformMatrix4fv(renderer->mvpLocation, 1, GL_FALSE, mvp);
  for (size_t i = 0; i < renderer->meshCount; i++) {
    const SectionGpuMesh *const mesh = &renderer->meshes[i];
    for (int t = 0; t < BLOCK_TYPE_COUNT; t++) {
      Vec3 color;
      if (!mesh->vertexCount[t] || !block_type_color((BlockType)t, &color)) continue;
      glUniform3f(renderer->colorLocation, color.x, color.y, color.z);
      glBindVertexArray(mesh->vao);
      __wrap_glDrawArrays(GL_TRIANGLES, mesh->firstVertex[t], mesh->vertexCount[t]);
    }
  }
}

static void assert_unique_gpu_owners(const Renderer *const renderer) {
  for (size_t i = 0; i < renderer->meshCount; i++) {
    const SectionGpuMesh *const mesh = &renderer->meshes[i];
    assert(!!mesh->vao == !!mesh->vbo);
    if (!mesh->vao) { assert(!vertex_count(mesh)); continue; }
    assert(glIsVertexArray(mesh->vao) && glIsBuffer(mesh->vbo));
    for (size_t j = i + 1; j < renderer->meshCount; j++) {
      assert(mesh->vao != renderer->meshes[j].vao);
      assert(mesh->vbo != renderer->meshes[j].vbo);
    }
  }
}

static void assert_unculled_parity(Renderer *const renderer, const Player *const player,
                                  const int expectCoverage) {
  static float depths[2][AUDIT_WIDTH * AUDIT_HEIGHT];
  static AuditRange optimized[AUDIT_RANGE_CAPACITY];
  size_t optimizedCount = 0;
  assert_unique_gpu_owners(renderer);
  renderer->framebufferWidth = AUDIT_WIDTH;
  renderer->framebufferHeight = AUDIT_HEIGHT;
  glViewport(0, 0, AUDIT_WIDTH, AUDIT_HEIGHT);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
  GLuint query = 0;
  glGenQueries(1, &query);
  for (int reference = 0; reference < 2; reference++) {
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    auditRenderer = renderer;
    auditRangeCount = 0;
    glBeginQuery(GL_PRIMITIVES_GENERATED, query);
    if (reference) render_unculled(renderer, player);
    else render_world(renderer, player);
    glEndQuery(GL_PRIMITIVES_GENERATED);
    auditRenderer = NULL;
    GLuint primitives = 0;
    glGetQueryObjectuiv(query, GL_QUERY_RESULT, &primitives);
    size_t triangles = 0;
    for (size_t i = 0; i < auditRangeCount; i++) triangles += (size_t)auditRanges[i].count / 3;
    assert(triangles == (size_t)primitives);
    glReadPixels(0, 0, AUDIT_WIDTH, AUDIT_HEIGHT, GL_DEPTH_COMPONENT, GL_FLOAT, depths[reference]);
    if (!reference) {
      optimizedCount = auditRangeCount;
      memcpy(optimized, auditRanges, optimizedCount * sizeof(*optimized));
    }
  }
  /* Every optimized range belongs to the reference, exactly once. */
  for (size_t i = 0; i < optimizedCount; i++) {
    size_t matches = 0;
    for (size_t j = 0; j < auditRangeCount; j++)
      matches += optimized[i].vao == auditRanges[j].vao &&
                 optimized[i].first == auditRanges[j].first && optimized[i].count == auditRanges[j].count;
    assert(matches == 1);
    for (size_t j = 0; j < i; j++)
      assert(optimized[i].vao != optimized[j].vao || optimized[i].first != optimized[j].first);
  }
  size_t covered = 0;
  for (size_t i = 0; i < AUDIT_WIDTH * AUDIT_HEIGHT; i++) {
    assert(isfinite(depths[0][i]) && isfinite(depths[1][i]));
    assert((depths[0][i] < 1.0f) == (depths[1][i] < 1.0f));
    assert(fabsf(depths[0][i] - depths[1][i]) <= 0.00001f);
    covered += depths[1][i] < 1.0f;
  }
  if (expectCoverage >= 0) assert((covered != 0) == expectCoverage);
  glDeleteQueries(1, &query);
  assert(glGetError() == GL_NO_ERROR);
}

static void test_unculled_transforms_seams_and_destruction(void) {
  const Vec3 origins[] = {{0}, {-57.25f, 31.5f, -83.0f}};
  const float scales[] = {1.0f, 2.5f};
  for (size_t fixture = 0; fixture < 2; fixture++) {
    World world = {0};
    Renderer renderer = {0};
    assert(world_init(&world, 128, origins[fixture], scales[fixture]));
    assert(renderer_init(&renderer));
    /* Both signs of X/Y and the negative Z section seam, with two materials. */
    for (int x = -2; x <= 1; x++)
      for (int y = -2; y <= 1; y++)
        for (int z = -17; z <= -16; z++)
          assert(world_set_block(&world, x, y, z, x < 0 ? BLOCK_TYPE_STONE : BLOCK_TYPE_SNOW));
    assert(renderer_sync_world(&renderer, &world));
    Player player = {.eyeHeight = 0.0f,
        .camera = {.front = {0, 0, -1}, .up = {0, 1, 0}}};
    const Vec3 positions[] = {{-0.5f, -0.5f, 0}, {-0.5f, -0.5f, -14.98f},
                              {-0.5f, -0.5f, 9.5f}, {-0.5f, -0.5f, 10.5f}};
    for (size_t pose = 0; pose < 4; pose++) {
      player.position = vec3_add(origins[fixture], vec3_scale(positions[pose], scales[fixture]));
      renderer.viewDistance = 25.0f * scales[fixture];
      assert_unculled_parity(&renderer, &player, pose == 1 ? -1 : pose != 3);
    }
    player.position = vec3_add(origins[fixture], (Vec3){0, 0, 0});
    renderer.viewDistance = MAX_VIEW_DISTANCE;
    /* Move the wall across both side planes and behind the camera. */
    const float yaws[] = {-0.8f, -0.7f, 0.0f, 0.7f, 0.8f, 3.14159265f};
    for (size_t pose = 0; pose < sizeof(yaws) / sizeof(*yaws); pose++) {
      player.camera.front = (Vec3){sinf(yaws[pose]), 0, -cosf(yaws[pose])};
      assert_unculled_parity(&renderer, &player, pose == 2 ? 1 : pose == 5 ? 0 : -1);
    }
    const float pitches[] = {-0.8f, -0.7f, 0.7f, 0.8f};
    for (size_t pose = 0; pose < sizeof(pitches) / sizeof(*pitches); pose++) {
      player.camera.front = (Vec3){0, sinf(pitches[pose]), -cosf(pitches[pose])};
      assert_unculled_parity(&renderer, &player, -1);
    }
    player.camera.front = (Vec3){0, 0, -1};
    player.position = vec3_add(origins[fixture], (Vec3){-0.5f * scales[fixture],
        -0.5f * scales[fixture], MAX_VIEW_DISTANCE - 15.0f * scales[fixture] - 0.5f});
    assert_unculled_parity(&renderer, &player, 1);
    player.position.z += 1.0f;
    assert_unculled_parity(&renderer, &player, 0);
    player.position = origins[fixture];
    for (int edit = 0; edit < 12; edit++) {
      assert(world_set_block(&world, -1, -1, -16,
                             edit % 2 ? BLOCK_TYPE_EMPTY : BLOCK_TYPE_GRASS));
      assert(renderer_sync_world(&renderer, &world));
      assert_unculled_parity(&renderer, &player, 1);
      for (size_t i = 0; i < renderer.meshCount; i++) {
        const SectionGpuMesh *const gpu = &renderer.meshes[i];
        SectionMesh cpu = {0};
        assert(section_mesh_build(&world, gpu->chunkX, gpu->sectionY, gpu->chunkZ, &cpu));
        assert_gpu_geometry(gpu, &cpu);
        section_mesh_destroy(&cpu);
      }
    }
    /* New earlier metadata identities transfer existing handles to one owner. */
    const SectionGpuMesh retained = *gpu_section(&renderer, -1, -1, -1);
    assert(world_set_block(&world, -40, 0, 0, BLOCK_TYPE_SAND));
    assert(renderer_sync_world(&renderer, &world));
    assert(gpu_section(&renderer, -1, -1, -1)->vbo == retained.vbo);
    assert_unique_gpu_owners(&renderer);
    GLuint vaos[32], vbos[32];
    const size_t count = renderer.meshCount;
    assert(count <= 32);
    for (size_t i = 0; i < count; i++) { vaos[i] = renderer.meshes[i].vao; vbos[i] = renderer.meshes[i].vbo; }
    const GLuint program = renderer.program, textVao = renderer.textVao, textVbo = renderer.textVbo;
    world_destroy(&world); /* CPU destruction does not own any GL objects. */
    assert_unique_gpu_owners(&renderer);
    renderer_destroy(&renderer);
    for (size_t i = 0; i < count; i++) assert(!glIsVertexArray(vaos[i]) && !glIsBuffer(vbos[i]));
    /* A current program remains flagged for deletion until it is unbound. */
    glUseProgram(0);
    assert(!glIsProgram(program) && !glIsVertexArray(textVao) && !glIsBuffer(textVbo));
    assert(!renderer.meshes && !renderer.visibleMeshes && !renderer.drawMeshes && !renderer.pendingCount);
    renderer_destroy(&renderer); /* Idempotent while the context remains current. */
    assert(glGetError() == GL_NO_ERROR);
  }
  puts("Unculled depth/range parity, transformed seams and GL destruction checks passed.");
}

static void test_rendered_pending_edits_and_cancellation(void) {
  Renderer renderer = {0};
  World world = {0}, referenceWorld = {0};
  Renderer reference = {0};
  Player player = {.position = {-0.5f, 400.5f, 0.0f}, .eyeHeight = 0.0f,
      .camera = {.front = {0, 0, -1}, .up = {0, 1, 0}}};
  const StreamBudget paused = {.operations = 0, .bytes = SIZE_MAX};
  const StreamBudget batch = {.operations = 16, .bytes = SIZE_MAX};
  assert(renderer_init(&renderer));
  renderer.viewDistance = 25.0f;
  assert(create_terrain_streamed(&world, 256, (Vec3){0}, 1.0f, WORLD_SEED));
  assert(world_set_block(&world, -1, 400, -16, BLOCK_TYPE_STONE));
  assert(world_set_block(&world, 0, 400, -16, BLOCK_TYPE_SNOW));
  assert(renderer_sync_view(&renderer, &world, &player));
  assert_unculled_parity(&renderer, &player, 1);
  static float oldDepth[AUDIT_WIDTH * AUDIT_HEIGHT], pendingDepth[AUDIT_WIDTH * AUDIT_HEIGHT];
  glReadPixels(0, 0, AUDIT_WIDTH, AUDIT_HEIGHT, GL_DEPTH_COMPONENT, GL_FLOAT, oldDepth);
  const SectionGpuMesh old = *gpu_section(&renderer, -1, 25, -1);
  SectionMesh oldCpu = {0};
  assert(section_mesh_build(&world, -1, 25, -1, &oldCpu));
  assert(world_set_block(&world, -1, 400, -16, BLOCK_TYPE_EMPTY));
  assert(renderer_sync_view_work(&renderer, &world, &player, &paused) == STREAM_DEFERRED);
  assert(world_get_block(&world, -1, 400, -16) == BLOCK_TYPE_EMPTY);
  assert_gpu_geometry(gpu_section(&renderer, -1, 25, -1), &oldCpu);
  assert_unculled_parity(&renderer, &player, 1);
  glReadPixels(0, 0, AUDIT_WIDTH, AUDIT_HEIGHT, GL_DEPTH_COMPONENT, GL_FLOAT, pendingDepth);
  for (size_t i = 0; i < AUDIT_WIDTH * AUDIT_HEIGHT; i++)
    assert(fabsf(oldDepth[i] - pendingDepth[i]) <= 0.00001f);
  section_mesh_destroy(&oldCpu);
  /* Fast departure cancels pending identities and deletes the old mesh before
     any new upload can reuse its names. Rendering submits no retired object. */
  player.position.x = 10000.0f;
  assert(renderer_sync_view_work(&renderer, &world, &player, &paused) == STREAM_COMPLETE);
  assert(!renderer.pendingCount && !glIsVertexArray(old.vao) && !glIsBuffer(old.vbo));
  assert_unculled_parity(&renderer, &player, 0);
  assert(!auditRangeCount);
  player.position.x = -0.5f;
  renderer.viewDistance = 100.0f;
  size_t passes = 0;
  while (renderer_sync_view_work(&renderer, &world, &player, &batch) != STREAM_COMPLETE)
    assert(++passes < 1000);
  /* Independent world, same edits: the synchronous drain is the convergence
     oracle, including driver-owned bytes and each material range. */
  assert(create_terrain_streamed(&referenceWorld, 256, (Vec3){0}, 1.0f, WORLD_SEED));
  assert(world_set_block(&referenceWorld, -1, 400, -16, BLOCK_TYPE_EMPTY));
  assert(world_set_block(&referenceWorld, 0, 400, -16, BLOCK_TYPE_SNOW));
  assert(renderer_init(&reference));
  reference.viewDistance = renderer.viewDistance;
  assert(renderer_sync_view(&reference, &referenceWorld, &player));
  assert(renderer.meshCount == reference.meshCount);
  for (size_t i = 0; i < reference.meshCount; i++) {
    const SectionGpuMesh *const expected = &reference.meshes[i];
    SectionMesh cpu = {0};
    assert(section_mesh_build(&referenceWorld, expected->chunkX, expected->sectionY, expected->chunkZ, &cpu));
    assert_gpu_geometry(expected, &cpu);
    assert_gpu_geometry(gpu_section(&renderer, expected->chunkX, expected->sectionY, expected->chunkZ), &cpu);
    section_mesh_destroy(&cpu);
  }
  assert_unculled_parity(&renderer, &player, 1);
  glReadPixels(0, 0, AUDIT_WIDTH, AUDIT_HEIGHT, GL_DEPTH_COMPONENT, GL_FLOAT, pendingDepth);
  assert_unculled_parity(&reference, &player, 1);
  glReadPixels(0, 0, AUDIT_WIDTH, AUDIT_HEIGHT, GL_DEPTH_COMPONENT, GL_FLOAT, oldDepth);
  for (size_t i = 0; i < AUDIT_WIDTH * AUDIT_HEIGHT; i++)
    assert(fabsf(oldDepth[i] - pendingDepth[i]) <= 0.00001f);
  renderer_destroy(&renderer);
  renderer_destroy(&reference);
  world_destroy(&world);
  world_destroy(&referenceWorld);
  puts("Rendered pending edits, fast departure and synchronous convergence checks passed.");
}

static void test_rendered_movement(void) {
  Renderer smokeRenderer = {0};
  Renderer *renderer = &smokeRenderer;
  assert(renderer_init(renderer));
  renderer->framebufferWidth = 640;
  renderer->framebufferHeight = 480;
  World world = {0};
  assert(create_platform(&world, 16, (Vec3){0}, 1, 1));
  for (int z = -8; z < 8; z++)
    for (int y = 0; y < 3; y++)
      assert(world_set_block(&world, 0, y, z, BLOCK_TYPE_STONE));
  assert(renderer_sync_world(renderer, &world));
  Player player = {.position = {-1, 0, 2}, .grounded = 1, .eyeHeight = 1,
                   .camera = {.front = {0, 0, -1}, .up = {0, 1, 0}}};
  DepthFrame initialView = capture_world(renderer, &player);
  assert_terrain_visible(&initialView);
  for (int frame = 0; frame < 60; frame++) {
    update_player(&player, (PlayerInput){.strafe = 1, .forward = 1},
                   1.0f / 60, &world);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    render_world(renderer, &player);
    assert(glGetError() == GL_NO_ERROR);
  }
  assert(fabsf(player.position.x + 0.25f) < 0.0001f);
  assert(player.position.z < -0.8f && player.grounded);
  DepthFrame groundedView = capture_world(renderer, &player);
  assert_view_changed(&initialView, &groundedView);
  free(initialView.depth);
  float peak = 0;
  int capturedAirborneView = 0;
  for (int frame = 0; frame < 60; frame++) {
    update_player(&player, (PlayerInput){.jump = frame == 0}, 1.0f / 60, &world);
    peak = fmaxf(peak, player.position.y);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    render_world(renderer, &player);
    assert(glGetError() == GL_NO_ERROR);
    if (!capturedAirborneView && player.position.y > 0.5f) {
      DepthFrame airborneView = capture_world(renderer, &player);
      assert_terrain_visible(&airborneView);
      assert_view_changed(&groundedView, &airborneView);
      free(airborneView.depth);
      capturedAirborneView = 1;
    }
  }
  glFinish();
  assert(peak > 1.15f && peak < 1.35f);
  assert(capturedAirborneView);
  assert(player.grounded && fabsf(player.position.y) < 0.0001f);
  DepthFrame landedView = capture_world(renderer, &player);
  assert_terrain_visible(&landedView);
  size_t changedAfterLanding = 0;
  for (size_t i = 0; i < groundedView.count; i++)
    changedAfterLanding += fabsf(groundedView.depth[i] - landedView.depth[i]) > 0.0001f;
  assert(changedAfterLanding <= groundedView.count / 1000);
  free(groundedView.depth);
  free(landedView.depth);
  puts("Rendered wall sliding and jump/landing checks passed.");
  renderer_destroy(renderer);
  world_destroy(&world);
}

static void test_view_distance_and_edits_change_rendered_geometry(void) {
  Renderer renderer = {0};
  World world = {0};
  assert(renderer_init(&renderer));
  renderer.framebufferWidth = 640;
  renderer.framebufferHeight = 480;
  assert(world_init(&world, 128, (Vec3){0}, 1));
  for (int x = -8; x <= 8; x++)
    for (int y = 0; y <= 16; y++)
      assert(world_set_block(&world, x, y, -50, BLOCK_TYPE_STONE));
  assert(renderer_sync_world(&renderer, &world));
  Player player = {.position = {0, 7.5f, 0}, .eyeHeight = 1,
      .camera = {.front = {0, 0, -1}, .up = {0, 1, 0}}};
  renderer.viewDistance = 25;
  DepthFrame tooFar = capture_world(&renderer, &player);
  assert(tooFar.covered == 0);
  renderer.viewDistance = 100;
  DepthFrame visible = capture_world(&renderer, &player);
  assert_terrain_visible(&visible);
  assert_view_changed(&tooFar, &visible);
  free(tooFar.depth);
  /* Failed initial allocation and failed growth both render complete geometry.
     Force growth after one collected section to exercise partial-list discard. */
  Performance p = {.enabled = 1};
  renderer.performance = &p;
  free(renderer.drawMeshes);
  renderer.drawMeshes = NULL;
  renderer.drawMeshCount = renderer.drawMeshCapacity = 0;
  for (int attempt = 0; attempt < 2; attempt++) {
    size_t *storage = renderer.drawMeshes;
    size_t capacity = renderer.drawMeshCapacity;
    draw_allocations_before_failure = 0;
    p.frame = (FrameWork){0};
    DepthFrame fallback = capture_world(&renderer, &player);
    draw_allocations_before_failure = -1;
    assert(p.frame.visibilityFallbacks == 1);
    assert(renderer.drawMeshes == storage && renderer.drawMeshCapacity == capacity);
    assert(!renderer.drawMeshCount);
    for (size_t i = 0; i < visible.count; i++)
      assert(fabsf(visible.depth[i] - fallback.depth[i]) < 0.00001f);
    free(fallback.depth);
    p.frame = (FrameWork){0};
    DepthFrame retry = capture_world(&renderer, &player);
    assert(!p.frame.visibilityFallbacks && renderer.drawMeshCount > 1);
    for (size_t i = 0; i < visible.count; i++)
      assert(fabsf(visible.depth[i] - retry.depth[i]) < 0.00001f);
    free(retry.depth);
    if (!attempt) renderer.drawMeshCapacity = 1;
  }
  renderer.performance = NULL;
  player.camera.front.z = 1;
  DepthFrame behind = capture_world(&renderer, &player);
  assert(!behind.covered && !renderer.drawMeshCount);
  free(behind.depth);
  player.camera.front.z = -1;
  free(visible.depth);
  renderer.viewDistance = 25;
  player.position.z = -24.5f; /* Wall straddles the far clip plane. */
  DepthFrame boundary = capture_world(&renderer, &player);
  assert_terrain_visible(&boundary);
  player.position.z = -23.5f;
  DepthFrame outsideBoundary = capture_world(&renderer, &player);
  assert(!outsideBoundary.covered);
  assert_view_changed(&boundary, &outsideBoundary);
  free(boundary.depth);
  free(outsideBoundary.depth);
  player.position.z = -30;
  DepthFrame approached = capture_world(&renderer, &player);
  assert_terrain_visible(&approached);
  free(approached.depth);
  for (int x = -8; x <= 8; x++)
    for (int y = 0; y <= 16; y++)
      assert(world_set_block(&world, x, y, -50, BLOCK_TYPE_EMPTY));
  assert(renderer_sync_world(&renderer, &world));
  DepthFrame removed = capture_world(&renderer, &player);
  assert(removed.covered == 0);
  free(removed.depth);
  renderer_destroy(&renderer);
  assert(!renderer.drawMeshes && !renderer.drawMeshCapacity);
  world_destroy(&world);
}

static void test_streamed_exploration_revisits_edited_terrain(void) {
  World world = {0};
  Renderer renderer = {0};
  uint32_t seed = UINT32_C(0x4B1D2A37);
  assert(create_terrain_streamed(&world, 1024, (Vec3){0}, 1, seed));
  assert(renderer_init(&renderer));
  renderer.framebufferWidth = 640;
  renderer.framebufferHeight = 480;
  renderer.viewDistance = 25;
  int height = terrain_sample_column(seed, 0, 0).height;
  Player player = {.position = {0.5f, (float)height + 8.0f, 4.5f}, .eyeHeight = 1,
      .camera = {.front = {0, -0.8944272f, -0.4472136f}, .up = {0, 1, 0}}};
  assert(renderer_sync_view(&renderer, &world, &player));
  DepthFrame before = capture_world(&renderer, &player);
  assert_terrain_visible(&before);
  assert(world_set_block(&world, 0, height, 0, BLOCK_TYPE_STONE));
  assert(renderer_sync_view(&renderer, &world, &player));
  DepthFrame edited = capture_world(&renderer, &player);
  assert_view_changed(&before, &edited);
  free(before.depth);
  Player originalView = player;
  for (int visit = 0; visit < 60; visit++) {
    player.position.x = (float)(visit % 8 - 4) * 96.0f + 0.5f;
    player.position.z = (float)(visit / 8 - 4) * 96.0f + 4.5f;
    player.position.y = world_surface_height(&world, player.position.x, player.position.z) + 8;
    assert(renderer_sync_view(&renderer, &world, &player));
    DepthFrame explored = capture_world(&renderer, &player);
    assert_terrain_visible(&explored);
    free(explored.depth);
    const int editX = (int)floorf(player.position.x);
    const int editZ = (int)floorf(player.position.z);
    if (editX || editZ != 4) {
      const int editY = terrain_sample_column(seed, editX, editZ).height;
      assert(world_set_block(&world, editX, editY, editZ,
                             (BlockType)(visit % BLOCK_TYPE_COUNT)));
    }
    size_t resident = 0;
    for (size_t c = 0; c < world.chunkCount; c++)
      resident += world.chunks[c].terrainReady || world.chunks[c].sections;
    assert(resident <= 49);
    assert(world.sectionCount <= 49 * WORLD_SECTION_COUNT);
    if (fabsf(player.position.x) > 64.0f || fabsf(player.position.z) > 64.0f)
      assert(!world_section(&world, 0, voxel_section_coord(height), 0));
    assert(world_get_block(&world, 0, height, 0) == BLOCK_TYPE_STONE);
  }
  player = originalView;
  assert(renderer_sync_view(&renderer, &world, &player));
  DepthFrame revisited = capture_world(&renderer, &player);
  assert(revisited.count == edited.count);
  for (size_t i = 0; i < edited.count; i++)
    assert(fabsf(revisited.depth[i] - edited.depth[i]) < 0.0001f);
  free(edited.depth);
  free(revisited.depth);
  assert(world_get_block(&world, 0, height, 0) == BLOCK_TYPE_STONE);
  size_t narrowSections = world.sectionCount;
  renderer.viewDistance = 300;
  assert(renderer_sync_view(&renderer, &world, &player));
  size_t wideSections = world.sectionCount;
  assert(wideSections > narrowSections);
  DepthFrame wide = capture_world(&renderer, &player);
  assert_terrain_visible(&wide);
  free(wide.depth);
  renderer.viewDistance = 25;
  assert(renderer_sync_view(&renderer, &world, &player));
  assert(world.sectionCount < wideSections);
  assert(world.sectionCount <= renderer.visibleMeshCount + WORLD_SECTION_COUNT);
  assert(world_get_block(&world, 0, height, 0) == BLOCK_TYPE_STONE);
  renderer_destroy(&renderer);
  world_destroy(&world);
}

/* Eviction also owns CPU columns omitted from renderer metadata, and cancels
   dirty mesh retries when leaving the neighbourhood. */
static void test_streamed_retirement_after_failed_sync(void) {
  World world = {0};
  Renderer renderer = {.viewDistance = 25};
  Performance p = {.enabled = 1};
  renderer.performance = &p;
  assert(create_terrain_streamed(&world, 1024, (Vec3){0}, 1, WORLD_SEED));
  Player player = {.position = {0.5f, 410, 0.5f}, .eyeHeight = 1};
  metadata_allocations_before_failure = 0;
  assert(!renderer_sync_view(&renderer, &world, &player));
  metadata_allocations_before_failure = -1;
  assert(world.sectionCount && !renderer.meshCount);
  player.position.x = 2000;
  assert(renderer_sync_view(&renderer, &world, &player));
  assert(!world.sectionCount); /* No GPU records existed to drive CPU eviction. */
  player.position.x = 0.5f;
  p.frame = (FrameWork){0};
  /* Explicitly materialized columns are subject to the same residency policy. */
  assert(world_materialize_chunk(&world, -24, 0));
  assert(world_section(&world, -24, WORLD_MIN_SECTION_Y, 0));
  assert(world_set_block(&world, 0, 400, 0, BLOCK_TYPE_STONE));
  assert(renderer_sync_view(&renderer, &world, &player));
  assert(!world_section(&world, -24, WORLD_MIN_SECTION_Y, 0));
  assert(p.frame.evictedChunks == 1);
  SectionGpuMesh *gpu = gpu_section(&renderer, 0, 25, 0);
  assert(gpu && vertex_count(gpu) == 36);
  GLuint vao = gpu->vao, vbo = gpu->vbo;
  const Subchunk *retained = world_section(&world, 0, 25, 0);
  assert(retained && !retained->dirty);
  SectionMesh oldMesh = {0}, baseline = {0};
  assert(section_mesh_build(&world, 0, 25, 0, &oldMesh));
  int baselineY = voxel_section_coord(terrain_sample_column(WORLD_SEED, 16, 0).height - 1);
  assert(section_mesh_build(&world, 1, baselineY, 0, &baseline));
  assert(baseline.vertexCount);
  assert_gpu_geometry(gpu, &oldMesh);
  assert_gpu_geometry(gpu_section(&renderer, 1, baselineY, 0), &baseline);

  /* Metadata growth is transactional even when old GPU resources are live. */
  assert(world_set_block(&world, 0, 416, 0, BLOCK_TYPE_GRAVEL));
  SectionGpuMesh *records = renderer.meshes;
  size_t recordCount = renderer.meshCount;
  metadata_allocations_before_failure = 0;
  assert(!renderer_sync_view(&renderer, &world, &player));
  metadata_allocations_before_failure = -1;
  assert(renderer.meshes == records && renderer.meshCount == recordCount);
  assert(world_section(&world, 0, 26, 0)->dirty);
  assert(world_get_block(&world, 0, 416, 0) == BLOCK_TYPE_GRAVEL);
  assert_gpu_geometry(gpu_section(&renderer, 0, 25, 0), &oldMesh);
  /* Metadata may commit before visible-index growth fails. Existing handles
     and index storage remain owned; no stale indices may be submitted. */
  size_t *visible = renderer.visibleMeshes;
  renderer.visibleMeshCapacity = renderer.meshCount;
  size_t capacity = renderer.visibleMeshCapacity;
  visible_allocations_before_failure = 0;
  assert(!renderer_sync_view(&renderer, &world, &player));
  visible_allocations_before_failure = -1;
  assert(renderer.visibleMeshes == visible && renderer.visibleMeshCapacity == capacity);
  assert(!renderer.visibleMeshCount && !renderer.drawMeshCount);
  assert(world_section(&world, 0, 26, 0)->dirty);
  gpu = gpu_section(&renderer, 0, 25, 0);
  assert(gpu->vbo == vbo && gpu->vao == vao);
  assert_gpu_geometry(gpu, &oldMesh);
  assert(renderer_sync_view(&renderer, &world, &player));
  assert(!world_section(&world, 0, 26, 0)->dirty);
  assert(world_set_block(&world, 0, 416, 0, BLOCK_TYPE_EMPTY));
  assert(renderer_sync_view(&renderer, &world, &player));
  gpu = gpu_section(&renderer, 0, 26, 0);
  assert(gpu && !gpu->vbo && !gpu->vao && !vertex_count(gpu));
  const Subchunk *marker = world_section(&world, 0, 26, 0);
  assert(marker && !marker->dirty && !marker->voxelCount);
  /* Removing the adjacent marker also rebuilds section 25. Capture its
     current ownership before testing transactional upload failure. */
  gpu = gpu_section(&renderer, 0, 25, 0);
  vao = gpu->vao;
  vbo = gpu->vbo;
  assert_gpu_geometry(gpu, &oldMesh);
  assert(world_set_block(&world, 0, 400, 0, BLOCK_TYPE_SNOW));
  real_buffer_upload = __glewBufferData;
  uploads_before_failure = 0;
  __glewBufferData = fail_buffer_upload;
  assert(!renderer_sync_view(&renderer, &world, &player));
  __glewBufferData = real_buffer_upload;
  assert(world_section(&world, 0, 25, 0)->dirty);
  gpu = gpu_section(&renderer, 0, 25, 0);
  assert(gpu->vbo == vbo && gpu->vao == vao);
  assert_gpu_geometry(gpu, &oldMesh);
  assert(world_get_block(&world, 0, 400, 0) == BLOCK_TYPE_SNOW);
  /* Leave the whole world: no new uploads can reuse the deleted GL names. */
  player.position.x = 2000;
  assert(renderer_sync_view(&renderer, &world, &player));
  assert(!glIsBuffer(vbo) && !glIsVertexArray(vao));
  assert(!renderer.visibleMeshCount && !renderer.drawMeshCount);
  assert(p.frame.retiredMeshes && p.frame.retiredBytes);
  for (size_t c = 0; c < world.chunkCount; c++) {
    const Chunk *chunk = &world.chunks[c];
    assert(!chunk->sections && !chunk->terrainReady);
  }
  assert(world_get_block(&world, 0, 400, 0) == BLOCK_TYPE_SNOW);
  assert(!world_section(&world, 0, 25, 0));
  assert(!world_section(&world, 0, 26, 0));
  assert_empty_gpu_world(&renderer);
  player.position.x = 0.5f;
  assert(renderer_sync_view(&renderer, &world, &player));
  gpu = gpu_section(&renderer, 0, 25, 0);
  assert(gpu && gpu->vertexCount[BLOCK_TYPE_SNOW] == 36);
  assert(world_section(&world, 0, 25, 0) && !world_section(&world, 0, 25, 0)->dirty);
  SectionMesh rebuilt = {0};
  assert(section_mesh_build(&world, 0, 25, 0, &rebuilt));
  assert(rebuilt.vertexCount == oldMesh.vertexCount);
  for (size_t v = 0; v < rebuilt.vertexCount; v++) {
    assert(rebuilt.vertices[v].x == oldMesh.vertices[v].x);
    assert(rebuilt.vertices[v].y == oldMesh.vertices[v].y);
    assert(rebuilt.vertices[v].z == oldMesh.vertices[v].z);
  }
  assert_gpu_geometry(gpu, &rebuilt);
  assert_gpu_geometry(gpu_section(&renderer, 1, baselineY, 0), &baseline);
  assert(!world_section(&world, 0, 26, 0));
  assert(world_get_block(&world, 0, 416, 0) == BLOCK_TYPE_EMPTY);
  section_mesh_destroy(&rebuilt);
  section_mesh_destroy(&oldMesh);
  section_mesh_destroy(&baseline);
  /* Every regenerated cell in an unedited column matches the seeded baseline. */
  for (int x = 16; x < 32; x++)
    for (int z = 0; z < 16; z++) {
      TerrainColumn column = terrain_sample_column(WORLD_SEED, x, z);
      for (int y = TERRAIN_BASE_Y; y < 384; y++)
        assert(world_get_block(&world, x, y, z) == terrain_column_block(column, y));
    }
  renderer_destroy(&renderer);
  world_destroy(&world);
}

static void test_performance_counters(void) {
  Application a = {0};
  a.performance.enabled = 1;
  assert(renderer_init(&a.renderer));
  a.renderer.performance = &a.performance;
  a.renderer.framebufferWidth = 640;
  a.renderer.framebufferHeight = 480;
  assert(world_init(&a.world, 16, (Vec3){0}, 1));
  assert(renderer_sync_world(&a.renderer, &a.world));
  Player player = {.position = {0, 0, 5}, .eyeHeight = 1,
      .camera = {.front = {0, 0, -1}, .up = {0, 1, 0}}};
  render_world(&a.renderer, &player);
  performance_snapshot(&a);
  assert(!a.residentChunks && !a.dirtySections && !a.gpuBytes);
  assert(!a.performance.frame.draws && !a.performance.frame.uploadCalls);
  assert(!a.performance.frame.distanceTests && !a.performance.frame.frustumTests);
  assert(!a.renderer.drawMeshCount && !a.renderer.drawMeshCapacity);
  assert(world_set_block(&a.world, 0, 0, 0, BLOCK_TYPE_STONE));
  performance_snapshot(&a);
  assert(a.residentChunks == 1 && a.dirtySections == 1);
  assert(renderer_sync_world(&a.renderer, &a.world));
  render_world(&a.renderer, &player);
  performance_snapshot(&a);
  assert(a.performance.frame.rebuilds == 1 && a.performance.frame.uploadCalls == 1);
  assert(a.performance.frame.uploadBytes == 36 * sizeof(SurfaceVertex));
  assert(a.performance.frame.peakMeshBytes == 36 * sizeof(SurfaceVertex));
  assert(a.performance.frame.draws == 1 && a.performance.frame.triangles == 12);
  assert(a.performance.frame.distanceTests == 1 && a.performance.frame.frustumTests == 1);
  /* Multiple materials in the same section still need just one visibility test. */
  assert(world_set_block(&a.world, 2, 0, 0, BLOCK_TYPE_GRASS));
  assert(renderer_sync_world(&a.renderer, &a.world));
  performance_reset(&a.performance);
  render_world(&a.renderer, &player);
  assert(a.performance.frame.distanceTests == 1 && a.performance.frame.frustumTests == 1);
  assert(a.performance.frame.draws == 2 && a.performance.frame.triangles == 24);
  assert(world_set_block(&a.world, 2, 0, 0, BLOCK_TYPE_EMPTY));
  assert(renderer_sync_world(&a.renderer, &a.world));
  performance_snapshot(&a);
  assert(a.gpuBytes == 36 * sizeof(SurfaceVertex) && !a.dirtySections);
  /* Failed requests count as work while committed storage stays unchanged. */
  assert(world_set_block(&a.world, 0, 0, 0, BLOCK_TYPE_GRASS));
  performance_reset(&a.performance);
  real_buffer_upload = __glewBufferData;
  uploads_before_failure = 0;
  __glewBufferData = fail_buffer_upload;
  int uploaded = renderer_sync_world(&a.renderer, &a.world);
  __glewBufferData = real_buffer_upload;
  assert(!uploaded);
  performance_snapshot(&a);
  assert(a.performance.frame.rebuilds == 1 && a.performance.frame.uploadCalls == 1);
  assert(a.performance.frame.uploadBytes == 36 * sizeof(SurfaceVertex));
  assert(a.gpuBytes == 36 * sizeof(SurfaceVertex) && a.dirtySections == 1);
  performance_reset(&a.performance);
  assert(renderer_sync_world(&a.renderer, &a.world));
  performance_snapshot(&a);
  assert(a.performance.frame.rebuilds == 1 && a.performance.frame.uploadCalls == 1);
  assert(a.performance.frame.uploadBytes == 36 * sizeof(SurfaceVertex));
  assert(a.gpuBytes == 36 * sizeof(SurfaceVertex) && !a.dirtySections);
  assert(world_set_block(&a.world, 0, 0, 0, BLOCK_TYPE_EMPTY));
  performance_reset(&a.performance);
  assert(renderer_sync_world(&a.renderer, &a.world));
  render_world(&a.renderer, &player);
  performance_snapshot(&a);
  assert(a.residentChunks == 1 && a.world.sectionCount == 1); /* Retirement marker. */
  assert(!a.gpuBytes && !a.performance.frame.uploadBytes && !a.performance.frame.draws);
  renderer_destroy(&a.renderer);
  world_destroy(&a.world);
}

static void test_sparse_vertical_renderer(void) {
  World world = {0};
  Renderer r = {0};
  assert(world_init(&world, 6144, (Vec3){0}, 1));
  assert(renderer_init(&r));
  assert(renderer_sync_world(&r, &world));
  assert(r.meshCount == 0); /* No metadata for all possible world sections. */
  const int ys[] = {-161, 416};
  for (int i = 0; i < 2; i++) {
    int y = ys[i];
    assert(world_set_block(&world, 0, y, 0, BLOCK_TYPE_SNOW));
    assert(renderer_sync_world(&r, &world));
    SectionGpuMesh *m = gpu_section(&r, 0, voxel_section_coord(y), 0);
    assert(m && m->vertexCount[BLOCK_TYPE_SNOW] == 36 && m->vao && m->vbo);
    assert(m->boundsMin.y == y && m->boundsMax.y == y + 1);
    assert(r.meshCount == world.sectionCount && r.meshCount == (size_t)i + 1);
    assert(world_set_block(&world, 0, y, 0, BLOCK_TYPE_EMPTY));
    assert(renderer_sync_world(&r, &world));
    assert(!m->vao && !m->vbo && vertex_count(m) == 0);
  }
  assert(world_init(&world, 6144, (Vec3){0}, 1));
  assert(renderer_sync_world(&r, &world));
  assert(!r.meshCount); /* Capacity remains reusable after the last departure. */
  renderer_destroy(&r);
  world_destroy(&world);
}

/* Counts, not wall-clock thresholds, define these scheduler assertions. */
static void test_stream_budget(void) {
  World world = {0};
  Renderer r = {0};
  Performance perf = {.enabled = 1};
  Player player = {.position = {-0.5f, 410.0f, -0.5f}, .eyeHeight = 1.0f};
  assert(create_terrain_streamed(&world, 256, (Vec3){0}, 1.0f, WORLD_SEED));
  assert(renderer_init(&r));
  r.performance = &perf;
  r.viewDistance = 100.0f;
  const StreamBudget paused = {.operations = 0, .bytes = SIZE_MAX};
  const StreamBudget one = {.operations = 1, .bytes = SIZE_MAX};
  const StreamBudget batch = {.operations = 256, .bytes = SIZE_MAX};
  assert(renderer_sync_view_work(&r, &world, &player, &paused) == STREAM_DEFERRED);
  assert(!world.sectionCount && r.pendingCount && r.pendingCount <= STREAM_TASK_CAPACITY);
  /* Nearest missing column wins, with signed identities and stable ties. */
  const StreamTask first = r.pending[0];
  assert(first.materialize && first.x == -1 && first.z == -1);
  perf.frame = (FrameWork){0};
  assert(renderer_sync_view_work(&r, &world, &player, &one) == STREAM_DEFERRED);
  assert(perf.frame.operations == 1 && stream_chunk(&world, -1, -1)->terrainReady);
  /* A zero byte budget also prevents starting work. */
  const StreamBudget noBytes = {.operations = 256, .bytes = 0};
  perf.frame = (FrameWork){0};
  assert(renderer_sync_view_work(&r, &world, &player, &noBytes) == STREAM_DEFERRED);
  assert(!perf.frame.operations);
  /* Materialize without meshing to exceed the bounded queue's capacity. */
  for (int x = world.minChunkX; x < world.minChunkX + world.chunksPerSide; x++)
    for (int z = world.minChunkZ; z < world.minChunkZ + world.chunksPerSide; z++)
      {
        assert(world_materialize_chunk(&world, x, z));
        for (int j = 0; j < 12; j++)
          assert(world_set_block(&world, x * SECTION_SIZE + 8, 500 + j * SECTION_SIZE,
                                 z * SECTION_SIZE + 8, BLOCK_TYPE_STONE));
      }
  assert(renderer_sync_view_work(&r, &world, &player, &paused) == STREAM_DEFERRED);
  assert(r.pendingCount == STREAM_TASK_CAPACITY && r.pendingWork > r.pendingCount);
  /* A long-waiting distant task overtakes newer nearby work. */
  const StreamTask aged = r.pending[r.pendingCount - 1];
  for (size_t i = 0; i < r.pendingCount; i++) r.pending[i].born = 10000;
  r.pending[r.pendingCount - 1].born = 0;
  r.streamFrame = 10000;
  assert(renderer_sync_view_work(&r, &world, &player, &paused) == STREAM_DEFERRED);
  assert(!stream_identity_compare(&aged, &r.pending[0]));
  size_t passes = 0;
  while (renderer_sync_view_work(&r, &world, &player, &batch) != STREAM_COMPLETE) {
    assert(r.pendingCount <= STREAM_TASK_CAPACITY && ++passes < 1000);
  }
  assert(passes > 1);
  for (size_t n = 0; n < r.visibleMeshCount; n++) {
    const SectionGpuMesh *const gpu = &r.meshes[r.visibleMeshes[n]];
    SectionMesh cpu = {0};
    assert(section_mesh_build(&world, gpu->chunkX, gpu->sectionY, gpu->chunkZ, &cpu));
    assert_gpu_geometry(gpu, &cpu);
    section_mesh_destroy(&cpu);
  }
  perf.frame = (FrameWork){0};
  assert(renderer_sync_view_work(&r, &world, &player, &one) == STREAM_COMPLETE);
  assert(!perf.frame.operations && !perf.frame.uploadCalls && !perf.frame.rebuilds);

  /* An edit is authoritative immediately; queued revisions refresh on retry. */
  assert(world_set_block(&world, -1, 410, -1, BLOCK_TYPE_STONE));
  assert(renderer_sync_view_work(&r, &world, &player, &paused) == STREAM_DEFERRED);
  const uint64_t revision = world_section(&world, -1, voxel_section_coord(410), -1)->revision;
  assert(world_set_block(&world, -1, 410, -1, BLOCK_TYPE_SNOW));
  assert(world_section(&world, -1, voxel_section_coord(410), -1)->revision > revision);
  assert(world_get_block(&world, -1, 410, -1) == BLOCK_TYPE_SNOW);
  while (renderer_sync_view_work(&r, &world, &player, &batch) != STREAM_COMPLETE) {}
  SectionGpuMesh *mesh = gpu_section(&r, -1, voxel_section_coord(410), -1);
  assert(mesh && mesh->vertexCount[BLOCK_TYPE_SNOW] == 36);
  const GLuint oldVbo = mesh->vbo;
  assert(world_set_block(&world, -1, 450, -1, BLOCK_TYPE_WATER));
  metadata_allocations_before_failure = 0;
  assert(renderer_sync_view_work(&r, &world, &player, &paused) == STREAM_FAILED);
  metadata_allocations_before_failure = -1;
  int oldVisible = 0;
  for (size_t n = 0; n < r.visibleMeshCount; n++)
    oldVisible |= r.meshes[r.visibleMeshes[n]].vbo == oldVbo;
  assert(oldVisible);
  r.visibleMeshCapacity = r.meshCount;
  visible_allocations_before_failure = 0;
  assert(renderer_sync_view_work(&r, &world, &player, &paused) == STREAM_FAILED);
  visible_allocations_before_failure = -1;
  oldVisible = 0;
  for (size_t n = 0; n < r.visibleMeshCount; n++)
    oldVisible |= r.meshes[r.visibleMeshes[n]].vbo == oldVbo;
  assert(oldVisible);
  while (renderer_sync_view_work(&r, &world, &player, &batch) != STREAM_COMPLETE) {}
  mesh = gpu_section(&r, -1, voxel_section_coord(410), -1);
  assert(world_set_block(&world, -1, 410, -1, BLOCK_TYPE_GRASS));
  assert(renderer_sync_view_work(&r, &world, &player, &paused) == STREAM_DEFERRED);
  assert(mesh->vbo == oldVbo && glIsBuffer(oldVbo));
  real_buffer_upload = __glewBufferData;
  uploads_before_failure = 0;
  __glewBufferData = fail_buffer_upload;
  assert(renderer_sync_view_work(&r, &world, &player, &one) == STREAM_FAILED);
  __glewBufferData = real_buffer_upload;
  assert(mesh->vbo == oldVbo && world_section(&world, -1, voxel_section_coord(410), -1)->dirty);
  assert(r.pendingCount);
  while (renderer_sync_view_work(&r, &world, &player, &batch) != STREAM_COMPLETE) {}
  SectionMesh reference = {0};
  assert(section_mesh_build(&world, -1, voxel_section_coord(410), -1, &reference));
  assert_gpu_geometry(gpu_section(&r, -1, voxel_section_coord(410), -1), &reference);
  section_mesh_destroy(&reference);

  /* Shrink, leave while work is queued, then revisit. No departing task survives. */
  assert(world_set_block(&world, 90, 410, 0, BLOCK_TYPE_SAND));
  assert(renderer_sync_view_work(&r, &world, &player, &paused) == STREAM_DEFERRED);
  r.viewDistance = 25.0f;
  assert(renderer_sync_view_work(&r, &world, &player, &paused) == STREAM_COMPLETE);
  assert(!r.pendingCount && world_get_block(&world, 90, 410, 0) == BLOCK_TYPE_SAND);
  player.position.x = 10000.0f;
  assert(renderer_sync_view_work(&r, &world, &player, &paused) == STREAM_COMPLETE);
  assert(!r.pendingCount && !r.visibleMeshCount);
  player.position.x = 90.0f;
  assert(world_set_block(&world, 90, 450, 0, BLOCK_TYPE_WATER));
  metadata_allocations_before_failure = 0;
  assert(renderer_sync_view_work(&r, &world, &player, &batch) == STREAM_FAILED);
  metadata_allocations_before_failure = -1;
  while (renderer_sync_view_work(&r, &world, &player, &batch) != STREAM_COMPLETE) {}
  assert(section_mesh_build(&world, voxel_section_coord(90), voxel_section_coord(410), 0, &reference));
  assert_gpu_geometry(gpu_section(&r, voxel_section_coord(90), voxel_section_coord(410), 0), &reference);
  section_mesh_destroy(&reference);
  renderer_destroy(&r);
  world_destroy(&world);
}

/* Accounting is enabled only around fresh owners, so fixture-only direct
   allocations elsewhere cannot affect the capacity/lifetime assertions. */
static void test_reusable_renderer_storage(void) {
  storage_accounting(1);
  World world = {0};
  Renderer r = {.viewDistance = 25.0f};
  Player player = {.position = {0.5f, 410.0f, 0.5f}};
  assert(create_terrain_streamed(&world, 256, (Vec3){0}, 1.0f, WORLD_SEED));
  for (int pass = 0; pass < 2; pass++)
    for (int step = 0; step < 8; step++) {
      player.position.x = (float)(step % 4) * 16.0f + 0.5f;
      assert(renderer_sync_view(&r, &world, &player));
    }
  size_t allocations[STORAGE_CATEGORY_COUNT];
  for (int c = 0; c < STORAGE_CATEGORY_COUNT; c++)
    allocations[c] = storage_stats((StorageCategory)c)->systemAllocations;
  for (int step = 0; step < 8; step++) {
    player.position.x = (float)(step % 4) * 16.0f + 0.5f;
    assert(renderer_sync_view(&r, &world, &player));
  }
  for (int c = 0; c < STORAGE_CATEGORY_COUNT; c++)
    assert(storage_stats((StorageCategory)c)->systemAllocations == allocations[c]);
  assert(storage_stats(STORAGE_MESH)->systemAllocations == 1);
  assert(!storage_stats(STORAGE_MESH)->live);
  assert(world_set_block(&world, 48, 500, 0, BLOCK_TYPE_SNOW));
  assert(renderer_sync_view(&r, &world, &player));
  SectionGpuMesh *const gpu = gpu_section(&r, 3, 31, 0);
  assert(gpu && gpu->vbo);
  const GLuint vbo = gpu->vbo;
  assert(world_set_block(&world, 48, 500, 0, BLOCK_TYPE_STONE));
  scratch_reservations_before_failure = 0;
  assert(!renderer_sync_view(&r, &world, &player));
  scratch_reservations_before_failure = -1;
  assert(gpu->vbo == vbo && glIsBuffer(vbo));
  assert(world_section(&world, 3, 31, 0)->dirty);
  assert(!storage_stats(STORAGE_MESH)->live);
  assert(renderer_sync_view(&r, &world, &player));
  assert(!glIsBuffer(vbo));
  size_t attempts = 0;
  for (int c = 0; c < STORAGE_CATEGORY_COUNT; c++) attempts += storage_stats((StorageCategory)c)->attempts;
  for (int frame = 0; frame < 16; frame++) assert(renderer_sync_view(&r, &world, &player));
  size_t after = 0;
  for (int c = 0; c < STORAGE_CATEGORY_COUNT; c++) after += storage_stats((StorageCategory)c)->attempts;
  assert(attempts == after);
  renderer_destroy(&r);
  world_destroy(&world);
  for (int c = 0; c < STORAGE_CATEGORY_COUNT; c++) {
    assert(!storage_stats((StorageCategory)c)->live);
    assert(!storage_stats((StorageCategory)c)->capacity);
  }
  storage_accounting(0);
}

int main(void) {
  storage_reject = reject_storage;
  test_frustum();
  glfwSetErrorCallback(glfw_test_error);
  glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
  assert(glfwInit());
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  GLFWwindow *window = glfwCreateWindow(640, 480, "Renderer tests", NULL, NULL);
  assert(window);
  glfwMakeContextCurrent(window);
  glewExperimental = GL_TRUE;
  assert(glewInit() == GLEW_OK);
  while (glGetError() != GL_NO_ERROR) {
  }
  printf("AUDIT compiler=%s vendor=%s renderer=%s version=%s display=%s resolution=640x480\n",
         __VERSION__, (const char *)glGetString(GL_VENDOR), (const char *)glGetString(GL_RENDERER),
         (const char *)glGetString(GL_VERSION), getenv("DISPLAY") ? getenv("DISPLAY") : "unset");
  test_unculled_transforms_seams_and_destruction();
  test_rendered_pending_edits_and_cancellation();
  test_stream_budget();
  test_performance_counters();
  test_sparse_vertical_renderer();
  Renderer renderer = {0};
  World world = {0};
  assert(renderer_init(&renderer));
  renderer.framebufferWidth = 640;
  renderer.framebufferHeight = 480;
  assert(world_init(&world, 32, (Vec3){0}, 1));
  assert(renderer_sync_world(&renderer, &world)); /* Empty worlds are valid. */
  assert_empty_gpu_world(&renderer);
  assert(world_set_block(&world, -1, 0, 0, BLOCK_TYPE_STONE));
  assert(world_set_block(&world, 0, 0, 0, BLOCK_TYPE_WATER));
  assert(renderer_sync_world(&renderer, &world));
  SectionGpuMesh *left = gpu_section(&renderer, -1, 0, 0);
  SectionGpuMesh *right = gpu_section(&renderer, 0, 0, 0);
  assert(left && right && vertex_count(left) == 30 && vertex_count(right) == 30);
  GLuint oldVao = left->vao, oldVbo = left->vbo;
  assert(world_set_block(&world, -1, 0, 0, BLOCK_TYPE_GRASS));
  real_buffer_upload = __glewBufferData;
  uploads_before_failure = 0;
  __glewBufferData = fail_buffer_upload;
  assert(!renderer_sync_world(&renderer, &world));
  __glewBufferData = real_buffer_upload;
  assert(left->vao == oldVao && left->vbo == oldVbo);
  assert(glIsVertexArray(oldVao) && glIsBuffer(oldVbo));
  assert(left->vertexCount[BLOCK_TYPE_STONE] == 30);
  assert(left->vertexCount[BLOCK_TYPE_GRASS] == 0);
  assert(world_get_block(&world, -1, 0, 0) == BLOCK_TYPE_GRASS);
  assert(world_section(&world, -1, 0, 0)->dirty);
  assert(renderer_sync_world(&renderer, &world));
  assert(!world_section(&world, -1, 0, 0)->dirty);
  assert(left->vertexCount[BLOCK_TYPE_GRASS] == 30);
  assert(left->vertexCount[BLOCK_TYPE_STONE] == 0);
  /* GL may immediately reuse the retired names for the neighbor's upload. */
  assert(!glIsVertexArray(oldVao) || right->vao == oldVao);
  assert(!glIsBuffer(oldVbo) || right->vbo == oldVbo);
  /* A later failure must not roll back an earlier section's successful upload. */
  oldVao = right->vao;
  oldVbo = right->vbo;
  assert(world_set_block(&world, -1, 0, 0, BLOCK_TYPE_SAND));
  assert(world_set_block(&world, 0, 0, 0, BLOCK_TYPE_STONE));
  uploads_before_failure = 1;
  __glewBufferData = fail_buffer_upload;
  assert(!renderer_sync_world(&renderer, &world));
  __glewBufferData = real_buffer_upload;
  assert(left->vertexCount[BLOCK_TYPE_SAND] == 30);
  assert(!world_section(&world, -1, 0, 0)->dirty);
  assert(right->vao == oldVao && right->vbo == oldVbo);
  assert(glIsVertexArray(oldVao) && glIsBuffer(oldVbo));
  assert(right->vertexCount[BLOCK_TYPE_WATER] == 30);
  assert(world_section(&world, 0, 0, 0)->dirty);
  assert(world_get_block(&world, 0, 0, 0) == BLOCK_TYPE_STONE);
  GLuint committedVao = left->vao, committedVbo = left->vbo;
  assert(renderer_sync_world(&renderer, &world));
  assert(left->vao == committedVao && left->vbo == committedVbo);
  assert(right->vertexCount[BLOCK_TYPE_STONE] == 30);
  assert(!world_section(&world, 0, 0, 0)->dirty);
  /* Removing the last voxel retires its GPU resources and exposes its neighbor. */
  oldVao = left->vao;
  oldVbo = left->vbo;
  assert(world_set_block(&world, -1, 0, 0, BLOCK_TYPE_EMPTY));
  assert(renderer_sync_world(&renderer, &world));
  assert(left->vao == 0 && left->vbo == 0 && vertex_count(left) == 0);
  assert(world_section(&world, -1, 0, 0)->voxelCount == 0);
  assert(!world_section(&world, -1, 0, 0)->dirty);
  /* GL may immediately reuse the retired names for the neighbor's upload. */
  assert(!glIsVertexArray(oldVao) || right->vao == oldVao);
  assert(!glIsBuffer(oldVbo) || right->vbo == oldVbo);
  assert(vertex_count(right) == 36);
  assert(world_set_block(&world, -1, 0, 0, BLOCK_TYPE_STONE));
  assert(renderer_sync_world(&renderer, &world));
  assert(vertex_count(left) == 30 && vertex_count(right) == 30);
  assert(glIsVertexArray(left->vao) && glIsBuffer(left->vbo));
  /* Allocated empty markers must also leave the entire scene without geometry. */
  assert(world_set_block(&world, -1, 0, 0, BLOCK_TYPE_EMPTY));
  assert(world_set_block(&world, 0, 0, 0, BLOCK_TYPE_EMPTY));
  assert(renderer_sync_world(&renderer, &world));
  assert(world.sectionCount == 2 && world.voxelCount == 0);
  assert_empty_gpu_world(&renderer);
  assert(world_set_block(&world, -1, 0, 0, BLOCK_TYPE_STONE));
  assert(world_set_block(&world, 0, 0, 0, BLOCK_TYPE_WATER));
  assert(renderer_sync_world(&renderer, &world));
  /* Regeneration may replace allocated sections with missing sections. */
  assert(world_init(&world, 32, (Vec3){0}, 1));
  assert(renderer_sync_world(&renderer, &world));
  assert(!gpu_section(&renderer, -1, 0, 0) && !gpu_section(&renderer, 0, 0, 0));
  assert(renderer.meshCount == 0);
  assert_empty_gpu_world(&renderer);
  renderer_destroy(&renderer);
  world_destroy(&world);

  assert(renderer_init(&renderer));
  renderer.framebufferWidth = 640;
  renderer.framebufferHeight = 480;
  double start = glfwGetTime();
  assert(create_terrain(&world, 192, (Vec3){0}, 1, UINT32_C(0x4B1D2A37)));
  assert(renderer_sync_world(&renderer, &world));
  double startup = 1000 * (glfwGetTime() - start);
  Player player = {.position = {0, 24, 32},
                   .eyeHeight = 1,
                   .camera = {.front = {0, -0.4472136f, -0.8944272f}, .up = {0, 1, 0}}};
  player.position.y = world_surface_height(&world, player.position.x, player.position.z) + 24;
  mat4 projection, view, mvp;
  mat4_perspective(75.0f * (3.14159f / 180), 640.0f / 480, 0.05f, 100, projection);
  Vec3 eye = player.position;
  eye.y += player.eyeHeight;
  mat4_lookat(eye, vec3_add(eye, player.camera.front), player.camera.up, view);
  mat4_multiply(projection, view, mvp);
  size_t draws = 0;
  for (size_t i = 0; i < renderer.meshCount; i++)
    if (section_in_frustum(&renderer.meshes[i], mvp))
      for (int t = 0; t < BLOCK_TYPE_COUNT; t++)
        draws += renderer.meshes[i].vertexCount[t] != 0;
  printf("Terrain submitted draws at benchmark camera: %zu\n", draws);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
  glViewport(0, 0, 640, 480);
  for (int i = 0; i < 10; i++) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    render_world(&renderer, &player);
  }
  glFinish();
  start = glfwGetTime();
  for (int i = 0; i < 120; i++) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    render_world(&renderer, &player);
    glFinish();
  }
  printf("GL renderer: %s; startup %.3f ms; frame %.3f ms\n", (const char *)glGetString(GL_RENDERER),
         startup, 1000 * (glfwGetTime() - start) / 120);
  unsigned char pixels[640 * 480 * 3];
  DepthFrame terrainFrame = capture_world(&renderer, &player);
  assert_terrain_visible(&terrainFrame);
  free(terrainFrame.depth);
  glReadPixels(0, 0, 640, 480, GL_RGB, GL_UNSIGNED_BYTE, pixels);
  FILE *file = fopen("/tmp/bwildering-renderer.ppm", "wb");
  assert(file);
  fprintf(file, "P6\n640 480\n255\n");
  for (int row = 479; row >= 0; row--)
    assert(fwrite(pixels + row * 640 * 3, 1, 640 * 3, file) == 640 * 3);
  fclose(file);
  assert(glGetError() == GL_NO_ERROR);
  renderer_destroy(&renderer);
  world_destroy(&world);
  test_rendered_movement();
  test_view_distance_and_edits_change_rendered_geometry();
  test_streamed_exploration_revisits_edited_terrain();
  test_streamed_retirement_after_failed_sync();
  test_reusable_renderer_storage();
  assert(glGetError() == GL_NO_ERROR);
  glfwDestroyWindow(window);
  glfwTerminate();
  /* Exercise the actual application startup, frame path, and shutdown as well. */
  assert(glfwInit());
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  Application application;
  double applicationStart = glfwGetTime();
  assert(application_init(&application));
  size_t terrainVertices = 0;
  for (size_t n = 0; n < application.renderer.visibleMeshCount; n++)
    terrainVertices += vertex_count(&application.renderer.meshes[application.renderer.visibleMeshes[n]]);
  printf("Default %dx%d world: startup %.1f ms, %zu resident sections, %.1f MiB terrain vertices\n",
         application.world.blocksPerSide, application.world.blocksPerSide,
         1000 * (glfwGetTime() - applicationStart), application.world.sectionCount,
         (double)(terrainVertices * sizeof(SurfaceVertex)) / (1024.0 * 1024.0));
  assert(application.world.blocksPerSide == PLATFORM_BLOCKS_PER_SIDE);
  float spawnY = world_surface_height(&application.world, application.player.position.x,
                                      application.player.position.z);
  assert(isfinite(spawnY) && application.player.position.y == spawnY);
  for (int i = 0; i < 60; i++)
    update_player(&application.player, (PlayerInput){0}, 1.0f / 60, &application.world);
  assert(application.player.grounded && application.player.position.y == spawnY);
  DepthFrame spawnView = capture_world(&application.renderer, &application.player);
  assert_terrain_visible(&spawnView);
  free(spawnView.depth);
  update_player(&application.player, (PlayerInput){.jump = 1}, 1.0f / 60, &application.world);
  assert(!application.player.grounded && application.player.position.y > spawnY);
  printf("Application spawn surface: %.1f; grounded and jump checks passed.\n", spawnY);
  /* A pending resize is applied exactly once, without polling window sizes. */
  const GLFWframebuffersizefun resize = glfwSetFramebufferSizeCallback(application.window, NULL);
  assert(resize);
  glfwSetFramebufferSizeCallback(application.window, resize);
  resize(application.window, 640, 480);
  assert(application.input.framebufferResized);
  windowSizeQueries = framebufferSizeQueries = viewportUpdates = 0;
  application_render(&application);
  GLint viewport[4];
  glGetIntegerv(GL_VIEWPORT, viewport);
  assert(viewport[2] == 640 && viewport[3] == 480);
  assert(viewportUpdates == 1 && !application.input.framebufferResized);
  assert(!windowSizeQueries && !framebufferSizeQueries);
  for (int frame = 0; frame < 3; frame++) {
    application_update(&application);
    windowSizeQueries = framebufferSizeQueries = viewportUpdates = 0;
    application_render(&application);
    assert(!windowSizeQueries && !framebufferSizeQueries && !viewportUpdates);
    assert(glGetError() == GL_NO_ERROR);
  }
  application_shutdown(&application);
  assert(!application.world.chunks && !application.renderer.meshes);
  puts("All renderer and application smoke tests passed.");
  return 0;
}
