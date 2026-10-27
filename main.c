#include "application.h"
#include "ui.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#pragma region Constants
/** World-space origin used to center the starter platform. */
static const Vec3 CENTER = {0.0f, 0.0f, 0.0f};
/** Width, height, and depth of each default platform block, in world units. */
static const float BLOCK_SIZE = 1.0f;
static const uint32_t WORLD_SEED = UINT32_C(0x4B1D2A37);
static const double FIXED_DELTA_TIME = 1.0 / 60.0;
static const double MAX_FRAME_DELTA_TIME = 0.25;
static const float DEFAULT_VIEW_DISTANCE = 100.0f;

#pragma endregion

#pragma region Camera
static void update_camera_front(Camera *const camera) {
  const float radYaw = camera->yaw * 3.14159265f / 180.0f;
  const float radPitch = camera->pitch * 3.14159265f / 180.0f;

  camera->front.x = cosf(radYaw) * cosf(radPitch);
  camera->front.y = sinf(radPitch);
  camera->front.z = sinf(radYaw) * cosf(radPitch);
  camera->front = vec3_normalize(camera->front);
}

static void update_camera(Camera *const camera, MouseInput *const input) {
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
static GLuint compile_shader(const GLenum type, const char *const source) {
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
  const char *const vertexShaderSource =
      "#version 330 core\n"
      "layout (location = 0) in vec3 aPos;\n"
      "uniform mat4 mvp;\n"
      "out vec3 worldPosition;\n"
      "void main() {\n"
      "    worldPosition = aPos;\n"
      "    gl_Position = mvp * vec4(aPos, 1.0);\n"
      "}\n";

  const char *const fragmentShaderSource =
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

static void section_gpu_mesh_destroy(SectionGpuMesh *const mesh) {
  if (mesh->vao)
    glDeleteVertexArrays(1, &mesh->vao);
  if (mesh->vbo)
    glDeleteBuffers(1, &mesh->vbo);
  *mesh = (SectionGpuMesh){0};
}

static void renderer_destroy(Renderer *const renderer) {
  for (size_t i = 0; i < renderer->meshCount; i++)
    section_gpu_mesh_destroy(&renderer->meshes[i]);
  free(renderer->meshes);
  free(renderer->visibleMeshes);
  free(renderer->drawMeshes);
  if (renderer->textVao != 0)
    glDeleteVertexArrays(1, &renderer->textVao);
  if (renderer->textVbo != 0)
    glDeleteBuffers(1, &renderer->textVbo);
  if (renderer->program != 0)
    glDeleteProgram(renderer->program);
  *renderer = (Renderer){0};
}

static int renderer_init(Renderer *const renderer) {
  *renderer = (Renderer){.viewDistance = DEFAULT_VIEW_DISTANCE};
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
               (GLsizeiptr)(UI_TEXT_MAX_CHARS * 5 * 7 * 6 * 3 * sizeof(float)), NULL,
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
static int renderer_upload_section_mesh(SectionGpuMesh *const target,
                                        const SectionMesh *const mesh, Performance *const p) {
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
    if (p && p->enabled) {
      p->frame.uploadCalls++;
      p->frame.uploadBytes += mesh->vertexCount * sizeof(*mesh->vertices);
    }
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

/* Synchronize before drawing. Each section replacement commits independently;
   failure leaves its previous mesh and dirty flag intact, stops this pass, and
   keeps earlier successful replacements. Collision already sees CPU edits.
   Missing/empty sections retire geometry without allocating a VAO/VBO for air.
   Records are keyed by explicit signed section identity. Residency changes
   merge the ordered collection and retire departing geometry. */
static int mesh_identity_compare(const SectionGpuMesh *const m, const int x, const int y, const int z) {
  if (m->chunkX != x) return m->chunkX < x ? -1 : 1;
  if (m->chunkZ != z) return m->chunkZ < z ? -1 : 1;
  return (m->sectionY > y) - (m->sectionY < y);
}

/* Resident records follow chunk X/Z and signed section Y, allowing a linear
   merge when residency changes. GPU handles move without replacing geometry. */
static int renderer_prepare_world(Renderer *const renderer, const World *const world) {
  size_t i = 0;
  int same = renderer->meshCount == world->sectionCount;
  for (size_t c = 0; same && c < world->chunkCount; c++)
    for (const Subchunk *s = world->chunks[c].sections; s; s = s->next)
      if (mesh_identity_compare(&renderer->meshes[i++], world->chunks[c].x, s->sectionY, world->chunks[c].z)) { same = 0; break; }
  if (same) return 1;

  const double start = perf_clock(renderer->performance);
  SectionGpuMesh *const records = world->sectionCount ? calloc(world->sectionCount, sizeof(*records)) : NULL;
  if (world->sectionCount && !records) return 0;

  /* Merge current section identities with the existing GPU records. */
  size_t old = 0, n = 0;
  for (size_t c = 0; c < world->chunkCount; c++)
    for (const Subchunk *s = world->chunks[c].sections; s; s = s->next) {
      const int x = world->chunks[c].x, z = world->chunks[c].z;
      while (old < renderer->meshCount && mesh_identity_compare(&renderer->meshes[old], x, s->sectionY, z) < 0)
        section_gpu_mesh_destroy(&renderer->meshes[old++]);
      if (old < renderer->meshCount && !mesh_identity_compare(&renderer->meshes[old], x, s->sectionY, z))
        records[n] = renderer->meshes[old++];
      else records[n] = (SectionGpuMesh){.chunkX = x, .sectionY = s->sectionY, .chunkZ = z};
      n++;
    }

  while (old < renderer->meshCount) section_gpu_mesh_destroy(&renderer->meshes[old++]);
  free(renderer->meshes);
  renderer->meshes = records;
  renderer->meshCount = n;
  renderer->metadataSeconds += perf_clock(renderer->performance) - start;
  return 1;
}

static int renderer_sync_section(const Renderer *const renderer, World *const world, const size_t i) {
  SectionGpuMesh *const gpu = &renderer->meshes[i];
  Subchunk *const section = world_section_mut(world, gpu->chunkX, gpu->sectionY, gpu->chunkZ);
  if (!section) {
    /* A regenerated world can omit a section that previously had a mesh. */
    section_gpu_mesh_destroy(gpu);
    return 1;
  }
  if (!section->dirty)
    return 1;

  SectionMesh cpu = {0};
  Performance *const p = renderer->performance;
  double start = perf_clock(p);
  int success = section_mesh_build(world, gpu->chunkX, gpu->sectionY, gpu->chunkZ, &cpu);
  if (p && p->enabled) {
    p->frame.mesh += perf_clock(p) - start;
    p->frame.rebuilds++;
    const size_t bytes = cpu.vertexCount * sizeof(*cpu.vertices);
    if (bytes > p->frame.peakMeshBytes) p->frame.peakMeshBytes = bytes;
  }

  if (success) {
    start = perf_clock(p);
    success = renderer_upload_section_mesh(gpu, &cpu, p);
    if (p && p->enabled) {
      p->frame.upload += perf_clock(p) - start;
    }
  }
  section_mesh_destroy(&cpu);
  if (!success)
    return 0;
  section->dirty = 0;
  return 1;
}

static int renderer_sync_world(Renderer *const renderer, World *const world) {
  if (!renderer_prepare_world(renderer, world))
    return 0;
  for (size_t i = 0; i < renderer->meshCount; i++)
    if (!renderer_sync_section(renderer, world, i))
      return 0;
  return 1;
}

static const Chunk *stream_chunk(const World *const world, const int x, const int z) {
  return &world->chunks[(size_t)(x - world->minChunkX) * (size_t)world->chunksPerSide +
                        (size_t)(z - world->minChunkZ)];
}

static int stream_identity_compare(const void *const a, const void *const b) {
  const StreamTask *const x = a, *const y = b;
  if (x->x != y->x) return x->x < y->x ? -1 : 1;
  if (x->z != y->z) return x->z < y->z ? -1 : 1;
  if (x->materialize != y->materialize) return x->materialize < y->materialize ? -1 : 1;
  return (x->y > y->y) - (x->y < y->y);
}

static int stream_priority_compare(const void *const a, const void *const b) {
  const StreamTask *const x = a, *const y = b;
  if (x->priority != y->priority) return x->priority < y->priority ? -1 : 1;
  return stream_identity_compare(a, b);
}

static void stream_admit(Renderer *const r, const StreamTask task, const size_t retained,
                         const size_t ordinal, const int pass, size_t *const nextCursor) {
  if ((ordinal < r->scanCursor) != (pass != 0) || r->pendingCount == STREAM_TASK_CAPACITY) return;
  if (bsearch(&task, r->pending, retained, sizeof(task), stream_identity_compare)) return;
  r->pending[r->pendingCount++] = task;
  *nextCursor = ordinal + 1;
}

static size_t stream_mesh_index(const Renderer *const r, const int x, const int y, const int z) {
  size_t lo = 0, hi = r->meshCount;
  while (lo < hi) {
    const size_t mid = lo + (hi - lo) / 2;
    const int cmp = mesh_identity_compare(&r->meshes[mid], x, y, z);
    if (!cmp) return mid;
    if (cmp < 0) lo = mid + 1; else hi = mid;
  }
  return r->meshCount;
}

/* Allocation failure can still draw every surviving old GL mesh using the
   existing index capacity. New records have no GL handles and need no slot. */
static void stream_visible_fallback(Renderer *const r, const int minX, const int maxX,
                                    const int minZ, const int maxZ) {
  r->visibleMeshCount = 0;
  for (size_t i = 0; i < r->meshCount && r->visibleMeshCount < r->visibleMeshCapacity; i++) {
    const SectionGpuMesh *const m = &r->meshes[i];
    if (m->vbo && m->chunkX >= minX && m->chunkX <= maxX && m->chunkZ >= minZ && m->chunkZ <= maxZ)
      r->visibleMeshes[r->visibleMeshCount++] = i;
  }
}

/* Sole residency owner for streamed gameplay: a square view radius in X/Z plus
   one chunk of padding, clipped to world bounds. Retire before allocating the
   next neighbourhood, including chunks with no renderer records (failed sync).
   Edited CPU columns remain authoritative; their derived GPU data can retire. */
static StreamResult renderer_sync_view_work(Renderer *const renderer, World *const world, const Player *const player,
                                            const StreamBudget *const budget) {
  if (!world->proceduralTerrain)
    return renderer_sync_world(renderer, world);
  const Vec3 eye = player_eye_position(player);
  if (!isfinite(eye.x) || !isfinite(eye.z))
    return 0;
  const float span = world->blockSize * SECTION_SIZE;
  const float distance = isfinite(renderer->viewDistance) && renderer->viewDistance > 0 ?
      fminf(renderer->viewDistance, MAX_VIEW_DISTANCE) : DEFAULT_VIEW_DISTANCE;
  const int maxChunkX = world->minChunkX + world->chunksPerSide - 1;
  const int maxChunkZ = world->minChunkZ + world->chunksPerSide - 1;
  const float loX = floorf((eye.x - world->origin.x - distance) / span) - 1;
  const float hiX = floorf((eye.x - world->origin.x + distance) / span) + 1;
  const float loZ = floorf((eye.z - world->origin.z - distance) / span) - 1;
  const float hiZ = floorf((eye.z - world->origin.z + distance) / span) + 1;
  const int minX = (int)fminf((float)maxChunkX + 1, fmaxf((float)world->minChunkX, loX));
  const int maxX = (int)fmaxf((float)world->minChunkX - 1, fminf((float)maxChunkX, hiX));
  const int minZ = (int)fminf((float)maxChunkZ + 1, fmaxf((float)world->minChunkZ, loZ));
  const int maxZ = (int)fmaxf((float)world->minChunkZ - 1, fminf((float)maxChunkZ, hiZ));

  /* No indices may survive metadata replacement or an interrupted sync pass. */
  renderer->streamed = 1;
  renderer->visibleMeshCount = renderer->drawMeshCount = 0;

  const double retirementStart = perf_clock(renderer->performance);
  /* Retire departing columns before collecting current section identities. */
  for (size_t i = 0; i < renderer->meshCount; i++) {
    SectionGpuMesh *const m = &renderer->meshes[i];
    if (m->chunkX >= minX && m->chunkX <= maxX && m->chunkZ >= minZ && m->chunkZ <= maxZ) continue;
    Subchunk *const section = world_section_mut(world, m->chunkX, m->sectionY, m->chunkZ);
    if (section) section->dirty = 1;
    if (renderer->performance && renderer->performance->enabled && m->vbo) {
      renderer->performance->frame.retiredMeshes++;
      for (int t = 0; t < BLOCK_TYPE_COUNT; t++)
        renderer->performance->frame.retiredBytes += (size_t)m->vertexCount[t] * sizeof(SurfaceVertex);
    }
    const int x = m->chunkX, y = m->sectionY, z = m->chunkZ;
    section_gpu_mesh_destroy(m);
    m->chunkX = x; m->sectionY = y; m->chunkZ = z;
  }

  /* CPU residency must not depend on successful renderer metadata allocation. */
  const size_t evicted = world_release_chunks_outside(world, minX, maxX, minZ, maxZ);
  if (renderer->performance && renderer->performance->enabled)
    renderer->performance->frame.evictedChunks += evicted;

  if (renderer->performance && renderer->performance->enabled)
    renderer->performance->frame.retirement += perf_clock(renderer->performance) - retirementStart;

  if (budget) {
    const double collectionStart = perf_clock(renderer->performance);
    /* Collection owns no section pointers. Invalidate departed/clean tasks and
       refresh revisions before executing; edits retain their original age. */
    renderer->streamFrame++;
    size_t n = 0;
    for (size_t i = 0; i < renderer->pendingCount; i++) {
      StreamTask task = renderer->pending[i];
      if (task.x < minX || task.x > maxX || task.z < minZ || task.z > maxZ) continue;
      const Chunk *const chunk = stream_chunk(world, task.x, task.z);
      const Subchunk *const section = task.materialize ? NULL : world_section(world, task.x, task.y, task.z);
      if (task.materialize ? chunk->terrainReady : (!section || !section->dirty)) continue;
      if (section) task.revision = section->revision;
      renderer->pending[n++] = task;
    }
    renderer->pendingCount = n;
    qsort(renderer->pending, n, sizeof(*renderer->pending), stream_identity_compare);
    const size_t retained = n;
    size_t ordinal = 0, nextCursor = renderer->scanCursor;
    renderer->pendingWork = 0;
    /* Two passes wrap the admission cursor without borrowing mutable records. */
    for (int pass = 0; pass < 2; pass++) {
      ordinal = 0;
      for (int x = minX; x <= maxX; x++) for (int z = minZ; z <= maxZ; z++) {
        const Chunk *const chunk = stream_chunk(world, x, z);
        if (!chunk->terrainReady) {
          const StreamTask task = {.x=x, .z=z, .materialize=1, .born=renderer->streamFrame};
          stream_admit(renderer, task, retained, ordinal++, pass, &nextCursor);
        }
        for (const Subchunk *section = chunk->sections; section; section = section->next) {
          if (!section->dirty) continue;
          const StreamTask task = {.x=x, .y=section->sectionY, .z=z,
              .revision=section->revision, .born=renderer->streamFrame};
          stream_admit(renderer, task, retained, ordinal++, pass, &nextCursor);
        }
      }
    }
    renderer->pendingWork = ordinal;
    renderer->scanCursor = ordinal ? nextCursor % ordinal : 0;
    if (renderer->pendingCount > renderer->peakPending) renderer->peakPending = renderer->pendingCount;
    const double eyeX = (double)(eye.x - world->origin.x) / (double)span;
    const double eyeZ = (double)(eye.z - world->origin.z) / (double)span;
    for (size_t i = 0; i < renderer->pendingCount; i++) {
      StreamTask *const task = &renderer->pending[i];
      const Chunk *const chunk = stream_chunk(world, task->x, task->z);
      /* Aging eventually dominates both distance and the edit preference. */
      task->priority = fabs((double)task->x + 0.5 - eyeX) + fabs((double)task->z + 0.5 - eyeZ)
          + (task->materialize ? 0.0 : fabs((double)task->y + 0.5 -
              (double)(eye.y - world->origin.y) / (double)span) * 0.25)
          - (double)(renderer->streamFrame - task->born) * 0.5 - (chunk->edited ? 64.0 : 0.0);
    }
    qsort(renderer->pending, renderer->pendingCount, sizeof(*renderer->pending), stream_priority_compare);
    if (renderer->performance && renderer->performance->enabled)
      renderer->performance->frame.metadata += perf_clock(renderer->performance) - collectionStart;
  } else {
    renderer->pendingCount = renderer->pendingWork = 0;
    for (int cx = minX; cx <= maxX; cx++) for (int cz = minZ; cz <= maxZ; cz++) {
      const int missing = !stream_chunk(world, cx, cz)->terrainReady;
      const double start = perf_clock(renderer->performance);
      const int ready = world_materialize_chunk(world, cx, cz);
      if (renderer->performance && renderer->performance->enabled) {
        FrameWork *const f = &renderer->performance->frame;
        const double elapsed = perf_clock(renderer->performance) - start;
        f->materialize += elapsed;
        if (missing) {
          f->materializedChunks += ready != 0;
          f->operations++;
          if (elapsed > f->atomicMax) f->atomicMax = elapsed;
        }
      }
      if (!ready) return STREAM_FAILED;
    }
  }

  const double metadataStart = perf_clock(renderer->performance);
  if (!renderer_prepare_world(renderer, world)) {
    if (budget) stream_visible_fallback(renderer, minX, maxX, minZ, maxZ);
    return STREAM_FAILED;
  }
  if (renderer->meshCount > renderer->visibleMeshCapacity) {
    size_t *const indices = malloc(renderer->meshCount * sizeof(*indices));
    if (!indices) {
      if (budget) stream_visible_fallback(renderer, minX, maxX, minZ, maxZ);
      return STREAM_FAILED;
    }
    free(renderer->visibleMeshes);
    renderer->visibleMeshes = indices;
    renderer->visibleMeshCapacity = renderer->meshCount;
  }

  /* Collect and synchronize only sections in the current neighborhood. */
  renderer->streamed = 1;
  renderer->visibleMeshCount = 0;
  for (size_t i = 0; i < renderer->meshCount; i++) {
    const SectionGpuMesh *const m = &renderer->meshes[i];
    if (m->chunkX < minX || m->chunkX > maxX || m->chunkZ < minZ || m->chunkZ > maxZ) continue;
    renderer->visibleMeshes[renderer->visibleMeshCount++] = i;

  }
  if (renderer->performance && renderer->performance->enabled)
    renderer->performance->frame.metadata += perf_clock(renderer->performance) - metadataStart;
  if (!budget) {
    for (size_t n = 0; n < renderer->visibleMeshCount; n++) {
      const size_t i = renderer->visibleMeshes[n];
      const SectionGpuMesh *const m = &renderer->meshes[i];
      const Subchunk *const section = world_section(world, m->chunkX, m->sectionY, m->chunkZ);
      const int dirty = section && section->dirty;
      const double start = perf_clock(renderer->performance);
      const int success = renderer_sync_section(renderer, world, i);
      if (dirty && renderer->performance && renderer->performance->enabled) {
        FrameWork *const f = &renderer->performance->frame;
        const double elapsed = perf_clock(renderer->performance) - start;
        f->operations++;
        if (elapsed > f->atomicMax) f->atomicMax = elapsed;
      }
      if (!success) return STREAM_FAILED;
    }
    return STREAM_COMPLETE;
  }

  const double workStart = glfwGetTime();
  size_t done = 0, bytes = 0;
  StreamResult result = STREAM_DEFERRED;
  while (done < renderer->pendingCount && done < budget->operations && bytes < budget->bytes) {
    if (budget->seconds > 0.0 && glfwGetTime() - workStart >= budget->seconds) break;
    const StreamTask task = renderer->pending[done];
    const double start = glfwGetTime();
    int success;
    if (task.materialize) {
      success = world_materialize_chunk(world, task.x, task.z);
      if (renderer->performance && renderer->performance->enabled) {
        renderer->performance->frame.materialize += glfwGetTime() - start;
        renderer->performance->frame.materializedChunks += success != 0;
      }
    } else {
      const Subchunk *const section = world_section(world, task.x, task.y, task.z);
      if (!section || !section->dirty || section->revision != task.revision) break;
      const size_t index = stream_mesh_index(renderer, task.x, task.y, task.z);
      if (index == renderer->meshCount) break; /* Materialized this pass; prepare next frame. */
      success = renderer_sync_section(renderer, world, index);
      for (int t = 0; t < BLOCK_TYPE_COUNT; t++)
        bytes += (size_t)renderer->meshes[index].vertexCount[t] * sizeof(SurfaceVertex);
    }
    if (renderer->performance && renderer->performance->enabled) {
      FrameWork *const f = &renderer->performance->frame;
      f->operations++;
      const double elapsed = glfwGetTime() - start;
      if (elapsed > f->atomicMax) f->atomicMax = elapsed;
    }
    if (!success) { result = STREAM_FAILED; break; }
    done++;
  }
  memmove(renderer->pending, renderer->pending + done,
          (renderer->pendingCount - done) * sizeof(*renderer->pending));
  renderer->pendingCount -= done;
  /* A materialization creates section work discovered on the next collection.
     Only an empty collection can certify stationary convergence. */
  return result == STREAM_FAILED ? result : renderer->pendingWork ? STREAM_DEFERRED : STREAM_COMPLETE;
}

/* Startup, deterministic captures and fixtures drain synchronously. */
static int renderer_sync_view(Renderer *const renderer, World *const world, const Player *const player) {
  return renderer_sync_view_work(renderer, world, player, NULL) == STREAM_COMPLETE;
}

static StreamResult renderer_step_view(Renderer *const renderer, World *const world, const Player *const player) {
  const StreamBudget budget = {.seconds = 0.002, .operations = 128, .bytes = 2 * 1024 * 1024};
  return renderer_sync_view_work(renderer, world, player, &budget);
}


/* Test the most positive AABB vertex against each homogeneous clip plane.
   No normalization is needed, and boxes crossing a plane remain visible. */
static int section_in_frustum(const SectionGpuMesh *const mesh, const float mvp[const 16]) {
  for(int axis=0;axis<3;axis++) for(int sign=-1;sign<=1;sign+=2) {
    const float a=mvp[3]+(float)sign*mvp[axis];
    const float b=mvp[7]+(float)sign*mvp[4+axis];
    const float c=mvp[11]+(float)sign*mvp[8+axis];
    const float d=mvp[15]+(float)sign*mvp[12+axis];
    const float x=a>=0 ? mesh->boundsMax.x : mesh->boundsMin.x;
    const float y=b>=0 ? mesh->boundsMax.y : mesh->boundsMin.y;
    const float z=c>=0 ? mesh->boundsMax.z : mesh->boundsMin.z;
    if(a*x+b*y+c*z+d<0) return 0;
  }
  return 1;
}

static int section_within_view_distance(const SectionGpuMesh *const mesh, const Vec3 eye,
                                         const float distance) {
  const float dx = fmaxf(mesh->boundsMin.x - eye.x, fmaxf(0, eye.x - mesh->boundsMax.x));
  const float dy = fmaxf(mesh->boundsMin.y - eye.y, fmaxf(0, eye.y - mesh->boundsMax.y));
  const float dz = fmaxf(mesh->boundsMin.z - eye.z, fmaxf(0, eye.z - mesh->boundsMax.z));
  return dx * dx + dy * dy + dz * dz <= distance * distance;
}

/* Allocation failure leaves storage owned and reusable. The caller falls back
   to direct visibility checks for this frame, never a stale/partial draw list. */
static int renderer_collect_draw_meshes(Renderer *const renderer, const Vec3 eye,
                                        const float distance, const float mvp[const 16]) {
  renderer->drawMeshCount = 0;
  const size_t count = renderer->streamed ? renderer->visibleMeshCount : renderer->meshCount;
  Performance *const p = renderer->performance;
  for (size_t n = 0; n < count; n++) {
    const size_t i = renderer->streamed ? renderer->visibleMeshes[n] : n;
    const SectionGpuMesh *const mesh = &renderer->meshes[i];
    int nonempty = 0;
    for (int t = 0; t < BLOCK_TYPE_COUNT; t++)
      nonempty |= mesh->vertexCount[t] != 0;
    if (!nonempty) continue;
    if (p && p->enabled) p->frame.distanceTests++;
    if (!section_within_view_distance(mesh, eye, distance)) continue;
    if (p && p->enabled) p->frame.frustumTests++;
    if (!section_in_frustum(mesh, mvp)) continue;
    if (renderer->drawMeshCount == renderer->drawMeshCapacity) {
      size_t capacity = renderer->drawMeshCapacity ? renderer->drawMeshCapacity * 2 : 64;
      if (capacity > count) capacity = count;
      size_t *const indices = realloc(renderer->drawMeshes, capacity * sizeof(*indices));
      if (!indices) {
        renderer->drawMeshCount = 0;
        if (p && p->enabled) p->frame.visibilityFallbacks++;
        return 0;
      }
      renderer->drawMeshes = indices;
      renderer->drawMeshCapacity = capacity;
    }
    renderer->drawMeshes[renderer->drawMeshCount++] = i;
  }
  return 1;
}

static void render_world(Renderer *const renderer, const Player *const player) {
  renderer->drawMeshCount = 0;
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

  Performance *const p = renderer->performance;
  const double start = perf_clock(p);
  const int collected = renderer_collect_draw_meshes(renderer, cameraPosition, viewDistance, mvp);
  if (p && p->enabled) p->frame.visibility += perf_clock(p) - start;

  /* Draw by material so all sections sharing a color use one uniform update. */
  glUniformMatrix4fv(renderer->mvpLocation, 1, GL_FALSE, (float *)mvp);
  for (int type = 0; type < BLOCK_TYPE_COUNT; type++) {
    Vec3 color;
    if (!block_type_color((BlockType)type, &color))
      continue;
    glUniform3f(renderer->colorLocation, color.x, color.y, color.z);
    if (p && p->enabled) p->frame.materialChanges++;
    const size_t count = collected ? renderer->drawMeshCount :
        (renderer->streamed ? renderer->visibleMeshCount : renderer->meshCount);
    for (size_t n = 0; n < count; n++) {
      const size_t i = collected ? renderer->drawMeshes[n] :
          (renderer->streamed ? renderer->visibleMeshes[n] : n);
      const SectionGpuMesh *const mesh = &renderer->meshes[i];
      if (!mesh->vertexCount[type]) continue;
      if (!collected) {
        if (p && p->enabled) p->frame.distanceTests++;
        if (!section_within_view_distance(mesh, cameraPosition, viewDistance)) continue;
        if (p && p->enabled) p->frame.frustumTests++;
        if (!section_in_frustum(mesh, mvp)) continue;
      }
      glBindVertexArray(mesh->vao);
      glDrawArrays(GL_TRIANGLES, mesh->firstVertex[type], mesh->vertexCount[type]);
      if (renderer->performance && renderer->performance->enabled) {
        renderer->performance->frame.draws++;
        renderer->performance->frame.triangles += (size_t)mesh->vertexCount[type] / 3;
      }
    }
  }
}

#pragma endregion

#pragma region Application
static void framebuffer_size_callback(GLFWwindow *const window, const int width,
                                      const int height) {
  Application *const application = glfwGetWindowUserPointer(window);
  if (application == NULL)
    return;
  application->renderer.framebufferWidth = width;
  application->renderer.framebufferHeight = height;
  glViewport(0, 0, width, height);
}

static void application_shutdown(Application *const application) {
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

static int application_init(Application *const application) {
  *application = (Application){
      .player = {.position = {.x = 0.5f, .y = 0.0f, .z = 0.5f},
                 .grounded = 1,
                 .eyeHeight = 1.0f,
                 .camera = {.yaw = -90.0f,
                            .pitch = 0.0f,
                            .front = {.x = 0.0f, .y = 0.0f, .z = -1.0f},
                            .up = {.x = 0.0f, .y = 1.0f, .z = 0.0f}}},
  };

  input_init(&application->input);

  int worldSide = PLATFORM_BLOCKS_PER_SIDE;
  const char *const configuredSide = getenv("BW_WORLD_SIZE");
  if (configuredSide) {
    char *end;
    const long parsed = strtol(configuredSide, &end, 10);
    if (end == configuredSide || *end || parsed < 1 || parsed > WORLD_MAX_BLOCKS_PER_SIDE) {
      fprintf(stderr, "BW_WORLD_SIZE must be an integer from 1 to %d\n",
              WORLD_MAX_BLOCKS_PER_SIDE);
      return 0;
    }
    worldSide = (int)parsed;
  }

  perf_configure(&application->performance);

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

  const double startupStart = perf_clock(&application->performance);
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
  input_attach(application->window);
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
  application->renderer.performance = &application->performance;

  glfwGetFramebufferSize(application->window,
                          &application->renderer.framebufferWidth,
                          &application->renderer.framebufferHeight);
  glViewport(0, 0, application->renderer.framebufferWidth,
             application->renderer.framebufferHeight);
  glfwSetFramebufferSizeCallback(application->window,
                                 framebuffer_size_callback);

  const double worldStart = perf_clock(&application->performance);
  if (!create_terrain_streamed(&application->world, worldSide, CENTER,
                       BLOCK_SIZE, WORLD_SEED)) {
    fprintf(stderr, "Failed to create voxel terrain\n");
    application_shutdown(application);
    return 0;
  }
  application->worldMetadataSeconds = perf_clock(&application->performance) - worldStart;

  /* Bounded deterministic search: prefer a grassy lowland near the start. */
  for (int ring = 0, found = 0; ring <= 64 && !found; ring++)
    for (int dx = -ring; dx <= ring && !found; dx++)
      for (int dz = -ring; dz <= ring && !found; dz++) {
        if (abs(dx) != ring && abs(dz) != ring) continue;
        const int x = (int)floorf((application->player.position.x - application->world.origin.x) / BLOCK_SIZE) + dx * 16;
        const int z = (int)floorf((application->player.position.z - application->world.origin.z) / BLOCK_SIZE) + dz * 16;
        if (x < application->world.minX || x >= application->world.minX + worldSide ||
            z < application->world.minZ || z >= application->world.minZ + worldSide) continue;
        const TerrainColumn c = terrain_sample_column(application->world.terrainSeed, x, z);
        if (c.material != BLOCK_TYPE_GRASS || c.height < 8 || c.height > 80) continue;
        application->player.position.x = application->world.origin.x + ((float)x + 0.5f) * BLOCK_SIZE;
        application->player.position.z = application->world.origin.z + ((float)z + 0.5f) * BLOCK_SIZE;
        found = 1;
      }
  if (!player_spawn_on_surface(&application->player, &application->world)) {
    fprintf(stderr, "Failed to find safe terrain for spawning\n");
    application_shutdown(application);
    return 0;
  }

  const double syncStart = perf_clock(&application->performance);
  if (!renderer_sync_view(&application->renderer, &application->world, &application->player)) {
    fprintf(stderr,"Failed to build or upload section meshes\n");
    application_shutdown(application);
    return 0;
  }

  application->performance.frame.sync = perf_clock(&application->performance) - syncStart;
  application->startupSeconds = perf_clock(&application->performance) - startupStart;
  if (application->performance.enabled) {
    fprintf(stdout, "PERF build=%s %s compiler=%s GL_vendor=%s GL_renderer=%s GL_version=%s\n",
            __DATE__, __TIME__, __VERSION__, (const char *)glGetString(GL_VENDOR),
            (const char *)glGetString(GL_RENDERER), (const char *)glGetString(GL_VERSION));
    application->performance.frame.raw = application->startupSeconds;
    perf_record(&application->performance);
    performance_report(application, "startup");
    performance_reset(&application->performance);
  }
  application->lastFrame = glfwGetTime();
  application->fpsWindowStart = application->lastFrame;
  return 1;
}

static void application_update(Application *const application) {
  GLFWwindow *const window = application->window;
  const int wasPaused = application->paused;
  glfwPollEvents();
  input_poll(&application->input, window);

  const double currentFrame = glfwGetTime();
  double deltaTime = currentFrame - application->lastFrame;
  application->rawFrame = fmax(0, deltaTime);
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

  /* Handle pause transitions before consuming gameplay input. */
  if (ui_handle_pause(application))
    return;
  if (application->paused || wasPaused) {
    application->simulationAccumulator = 0;
    application->jumpQueued = 0;
    application->placeQueued = 0;
    application->removeQueued = 0;
    input_discard(&application->input);
    return;
  }

  /* Retain a new jump press until a simulation tick can consume it. */
  update_camera(&application->player.camera, &application->input.mouse);
  PlayerInput input = input_player(&application->input);
  if (application->input.pressed & INPUT_JUMP)
    application->jumpQueued = 1;
  application->placeQueued |= (application->input.pressed & INPUT_PLACE) != 0;
  application->removeQueued |= (application->input.pressed & INPUT_REMOVE) != 0;
  input.jump = 0;

  /* Advance physics at a fixed rate, independent of rendering frequency. */
  application->simulationAccumulator += deltaTime;
  while (application->simulationAccumulator >= FIXED_DELTA_TIME) {
    input.jump = application->jumpQueued;
    update_player(&application->player, input, (float)FIXED_DELTA_TIME,
                  &application->world);
    application->jumpQueued = 0;
    application->simulationAccumulator -= FIXED_DELTA_TIME;
  }

  /* Apply queued block edits once per rendered frame. */
  if (application->placeQueued) {
    if (glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED)
      player_place_block(&application->world, &application->player, BLOCK_TYPE_STONE, 6.0f);
    application->placeQueued = 0;
  }
  if (application->removeQueued) {
    if (glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED)
      player_remove_block(&application->world, &application->player, 6.0f);
    application->removeQueued = 0;
  }
}

static void application_render(Application *const application) {
  Performance *const p = &application->performance;
  double start = perf_clock(p);
  glClearColor(0.15f, 0.15f, 0.2f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  render_world(&application->renderer, &application->player);
  ui_render(application);
  if (p->enabled) p->frame.render += perf_clock(p) - start;

  start = perf_clock(p);
  glfwSwapBuffers(application->window);
  if (p->enabled) p->frame.swap += perf_clock(p) - start;
}

#pragma endregion

/* Raw duration is the previous start-to-start interval, before simulation clamping.
   Work fields describe this frame; a spike appears in raw on the next frame. */
static int application_frame(Application *const a) {
  Performance *const p = &a->performance;
  p->frame = (FrameWork){0};
  double start = perf_clock(p);
  application_update(a);
  if (p->enabled) {
    p->frame.raw = a->rawFrame;
    p->frame.update = perf_clock(p) - start;
  }

  start = perf_clock(p);
  const StreamResult result = renderer_step_view(&a->renderer, &a->world, &a->player);
  if (p->enabled) p->frame.sync = perf_clock(p) - start;

  application_render(a);
  perf_record(p);
  /* Failed transactional work remains queued for retry next frame. */
  return result != STREAM_FAILED;
}

#pragma region Main
int main(void) {
  Application application;
  if (!application_init(&application))
    return EXIT_FAILURE;

  int meshRebuildFailed = 0;
  while (!glfwWindowShouldClose(application.window)) {
    if (!application_frame(&application)) {
      if (!meshRebuildFailed)
        fprintf(stderr, "Failed to rebuild dirty section meshes; retaining previous "
                        "meshes and retrying\n");
      meshRebuildFailed = 1;
    } else {
      meshRebuildFailed = 0;
    }
    Performance *const p = &application.performance;
    if (p->enabled && (p->count == PERF_SAMPLES || p->total.raw >= 1.0)) {
      performance_report(&application, "play");
      performance_reset(p);
    }
  }
  if (application.performance.count) performance_report(&application, "play-final");

  application_shutdown(&application);
  return EXIT_SUCCESS;
}
#pragma endregion
