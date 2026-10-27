#include "renderer_internal.h"
#include "storage.h"
#include "view_settings.h"
#include <GLFW/glfw3.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Only selected observation helpers have external linkage in fixture builds. */
#ifdef BW_RENDERER_TEST
#define RENDERER_PRIVATE
#else
#define RENDERER_PRIVATE static
#endif

#pragma region Rendering
RENDERER_PRIVATE GLuint compile_shader(const GLenum type, const char *const source) {
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

RENDERER_PRIVATE GLuint create_shader_program(void) {
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
      "uniform vec4 blockGrid; // World origin and inverse block size; zero disables detail for UI.\n"
      "in vec3 worldPosition;\n"
      "uint blockHash(ivec3 cell) {\n"
      "    uvec3 p = uvec3(cell);\n"
      "    uint h = p.x * 0x9e3779b9u ^ p.y * 0x85ebca6bu ^ p.z * 0xc2b2ae35u;\n"
      "    h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15;\n"
      "    return h;\n"
      "}\n"
      "void main() {\n"
      "    vec3 normal = normalize(cross(dFdx(worldPosition), dFdy(worldPosition)));\n"
      "    if (!gl_FrontFacing) normal = -normal;\n"
      "    float light = 0.45 + 0.55 * max(dot(normal, normalize(vec3(-0.4, 1.0, 0.3))), 0.0);\n"
      "    vec3 tint = color;\n"
      "    if (blockGrid.w > 0.0) {\n"
      "        vec3 grid = (worldPosition - blockGrid.xyz) * blockGrid.w;\n"
      "        uint h = blockHash(ivec3(floor(grid - normal * 0.001)));\n"
      "        float shade = float(h & 1023u) / 1023.0 - 0.5;\n"
      "        float warmth = float((h >> 10) & 1023u) / 1023.0 - 0.5;\n"
      "        tint = color * (1.0 + 0.05 * shade) + vec3(0.018, 0.003, -0.014) * warmth;\n"
      "        vec3 axis = abs(normal);\n"
      "        vec2 face = axis.x > 0.5 ? grid.zy : axis.y > 0.5 ? grid.xz : grid.xy;\n"
      "        vec2 pixel = max(fwidth(face), vec2(0.0001));\n"
      "        vec2 uv = fract(face);\n"
      "        vec2 edgeDistance = min(uv, 1.0 - uv);\n"
      "        vec2 border = 1.0 - smoothstep(vec2(0.012) - pixel, vec2(0.028) + pixel, edgeDistance);\n"
      "        float fade = 1.0 - smoothstep(0.04, 0.18, max(pixel.x, pixel.y));\n"
      "        float edge = max(border.x, border.y) * fade;\n"
      "        vec2 rim = 1.0 - smoothstep(vec2(0.025) - pixel, vec2(0.055) + pixel, vec2(uv.x, 1.0 - uv.y));\n"
      "        float accent = max(rim.x, rim.y) * fade * (1.0 - edge);\n"
      "        tint *= 1.0 - 0.10 * edge + 0.025 * accent;\n"
      "    }\n"
      "    FragColor = vec4(tint * light, 1.0);\n"
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

RENDERER_PRIVATE void section_gpu_mesh_destroy(SectionGpuMesh *const mesh) {
  if (mesh->vao)
    glDeleteVertexArrays(1, &mesh->vao);
  if (mesh->vbo)
    glDeleteBuffers(1, &mesh->vbo);
  *mesh = (SectionGpuMesh){0};
}

void renderer_destroy(Renderer *const renderer) {
  for (size_t i = 0; i < renderer->meshCount; i++)
    section_gpu_mesh_destroy(&renderer->meshes[i]);
  storage_release(STORAGE_RECORD, renderer->meshCount * sizeof(*renderer->meshes));
  storage_backing_free(STORAGE_RECORD, renderer->meshes, renderer->meshCapacity * sizeof(*renderer->meshes));
  storage_backing_free(STORAGE_RECORD, renderer->staging, renderer->stagingCapacity * sizeof(*renderer->staging));
  storage_backing_free(STORAGE_MESH, renderer->meshScratch, SECTION_MESH_MAX_VERTICES * sizeof(*renderer->meshScratch));
  storage_free(STORAGE_VISIBLE, renderer->visibleMeshes, renderer->visibleMeshCapacity * sizeof(*renderer->visibleMeshes));
  storage_free(STORAGE_DRAW, renderer->drawMeshes, renderer->drawMeshCapacity * sizeof(*renderer->drawMeshes));
  if (renderer->textVao != 0)
    glDeleteVertexArrays(1, &renderer->textVao);
  if (renderer->textVbo != 0)
    glDeleteBuffers(1, &renderer->textVbo);
  if (renderer->program != 0)
    glDeleteProgram(renderer->program);
  *renderer = (Renderer){0};
}

int renderer_init(Renderer *const renderer) {
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
  renderer->blockGridLocation = glGetUniformLocation(renderer->program, "blockGrid");
  if (renderer->blockGridLocation < 0) {
    fprintf(stderr, "Failed to locate block grid uniform\n");
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
        (Vec3){.x = mesh->vertices[0].x, .y = mesh->vertices[0].y, .z = mesh->vertices[0].z};
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
  renderer->worldOrigin = world->origin;
  renderer->blockSize = world->blockSize;
  size_t i = 0;
  int same = renderer->meshCount == world->sectionCount;
  for (size_t c = 0; same && c < world->chunkCount; c++)
    for (const Subchunk *s = world->chunks[c].sections; s; s = s->next)
      if (mesh_identity_compare(&renderer->meshes[i++], world->chunks[c].x, s->sectionY, world->chunks[c].z)) { same = 0; break; }
  if (same) return 1;

  const double start = perf_clock(renderer->performance);
  if (world->sectionCount > SIZE_MAX / sizeof(SectionGpuMesh)) return 0;
  const size_t bytes = world->sectionCount * sizeof(SectionGpuMesh);
  if (!storage_reserve(STORAGE_RECORD, bytes, world->sectionCount <= renderer->stagingCapacity)) return 0;
  if (world->sectionCount > renderer->stagingCapacity) {
    size_t capacity = renderer->stagingCapacity ? renderer->stagingCapacity : 64;
    while (capacity < world->sectionCount && capacity <= SIZE_MAX / 2) capacity *= 2;
    if (capacity < world->sectionCount || capacity > SIZE_MAX / sizeof(SectionGpuMesh)) {
      storage_release(STORAGE_RECORD, bytes);
      return 0;
    }
    SectionGpuMesh *const replacement = storage_backing_alloc(STORAGE_RECORD, capacity * sizeof(*replacement));
    if (!replacement) { storage_release(STORAGE_RECORD, bytes); return 0; }
    storage_backing_free(STORAGE_RECORD, renderer->staging, renderer->stagingCapacity * sizeof(*replacement));
    renderer->staging = replacement;
    renderer->stagingCapacity = capacity;
  }
  SectionGpuMesh *const records = renderer->staging;

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
  storage_release(STORAGE_RECORD, renderer->meshCount * sizeof(*renderer->meshes));
  renderer->staging = renderer->meshes;
  renderer->meshes = records;
  const size_t oldCapacity = renderer->meshCapacity;
  renderer->meshCapacity = renderer->stagingCapacity;
  renderer->stagingCapacity = oldCapacity;
  /* Staging is spare capacity only; copied GL handles have exactly one owner. */
  if (renderer->staging) memset(renderer->staging, 0, renderer->stagingCapacity * sizeof(*renderer->staging));
  renderer->meshCount = n;
  renderer->metadataSeconds += perf_clock(renderer->performance) - start;
  return 1;
}

static int renderer_sync_section(Renderer *const renderer, const World *const world, const size_t i) {
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
  const size_t scratchBytes = SECTION_MESH_MAX_VERTICES * sizeof(*renderer->meshScratch);
  if (!storage_reserve(STORAGE_MESH, scratchBytes, renderer->meshScratch != NULL)) return 0;
  if (!renderer->meshScratch) renderer->meshScratch = storage_backing_alloc(STORAGE_MESH, scratchBytes);
  int success = renderer->meshScratch && section_mesh_build_scratch(world, gpu->chunkX,
      gpu->sectionY, gpu->chunkZ, renderer->meshScratch, SECTION_MESH_MAX_VERTICES, &cpu);
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
  storage_release(STORAGE_MESH, scratchBytes);
  if (!success)
    return 0;
  section->dirty = 0;
  return 1;
}

RENDERER_PRIVATE int renderer_sync_world(Renderer *const renderer, const World *const world) {
  if (!renderer_prepare_world(renderer, world))
    return 0;
  for (size_t i = 0; i < renderer->meshCount; i++)
    if (!renderer_sync_section(renderer, world, i))
      return 0;
  return 1;
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

static void renderer_stream_retire(void *const context, World *const world, const StreamRegion region) {
  Renderer *const renderer = context;
  const int minX = region.minX, maxX = region.maxX, minZ = region.minZ, maxZ = region.maxZ;
  /* No indices may survive metadata replacement or an interrupted sync pass. */
  renderer->streamed = 1;
  renderer->visibleMeshCount = renderer->drawMeshCount = 0;

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
}

static int renderer_stream_prepare(void *const context, const World *const world,
                                   const StreamRegion region, const int bounded) {
  Renderer *const renderer = context;
  const int minX = region.minX, maxX = region.maxX, minZ = region.minZ, maxZ = region.maxZ;
  const double metadataStart = perf_clock(renderer->performance);
  if (!renderer_prepare_world(renderer, world)) {
    if (bounded) stream_visible_fallback(renderer, minX, maxX, minZ, maxZ);
    return 0;
  }
  if (renderer->meshCount > renderer->visibleMeshCapacity) {
    size_t capacity = renderer->visibleMeshCapacity ? renderer->visibleMeshCapacity : 64;
    while (capacity < renderer->meshCount && capacity <= SIZE_MAX / 2) capacity *= 2;
    if (capacity < renderer->meshCount || capacity > SIZE_MAX / sizeof(size_t)) {
      if (bounded) stream_visible_fallback(renderer, minX, maxX, minZ, maxZ);
      return 0;
    }
    size_t *const indices = storage_alloc(STORAGE_VISIBLE, capacity * sizeof(*indices));
    if (!indices) {
      if (bounded) stream_visible_fallback(renderer, minX, maxX, minZ, maxZ);
      return 0;
    }
    storage_free(STORAGE_VISIBLE, renderer->visibleMeshes, renderer->visibleMeshCapacity * sizeof(*renderer->visibleMeshes));
    renderer->visibleMeshes = indices;
    renderer->visibleMeshCapacity = capacity;
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
  return 1;
}

static int renderer_stream_drain(void *const context, const World *const world) {
  Renderer *const renderer = context;
  if (!world->proceduralTerrain) return renderer_sync_world(renderer, world);
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

static StreamResult renderer_stream_sync(void *const context, const World *const world,
                                         const StreamTask task, size_t *const bytes) {
  Renderer *const renderer = context;
  const size_t index = stream_mesh_index(renderer, task.x, task.y, task.z);
  if (index == renderer->meshCount) return STREAM_DEFERRED;
  const int success = renderer_sync_section(renderer, world, index);
  for (int t = 0; t < BLOCK_TYPE_COUNT; t++)
    *bytes += (size_t)renderer->meshes[index].vertexCount[t] * sizeof(SurfaceVertex);
  return success ? STREAM_COMPLETE : STREAM_FAILED;
}

StreamConsumer renderer_stream_consumer(Renderer *const renderer) {
  return (StreamConsumer){renderer, renderer_stream_retire, renderer_stream_prepare,
                          renderer_stream_sync, renderer_stream_drain};
}

/* Test the most positive AABB vertex against each homogeneous clip plane.
   No normalization is needed, and boxes crossing a plane remain visible. */
RENDERER_PRIVATE int section_in_frustum(const SectionGpuMesh *const mesh, const float mvp[const 16]) {
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

RENDERER_PRIVATE int section_within_view_distance(const SectionGpuMesh *const mesh, const Vec3 eye,
                                         const float distance) {
  const float dx = fmaxf(mesh->boundsMin.x - eye.x, fmaxf(0, eye.x - mesh->boundsMax.x));
  const float dy = fmaxf(mesh->boundsMin.y - eye.y, fmaxf(0, eye.y - mesh->boundsMax.y));
  const float dz = fmaxf(mesh->boundsMin.z - eye.z, fmaxf(0, eye.z - mesh->boundsMax.z));
  return dx * dx + dy * dy + dz * dz <= distance * distance;
}

/* Allocation failure leaves storage owned and reusable. The caller falls back
   to direct visibility checks for this frame, never a stale/partial draw list. */
RENDERER_PRIVATE int renderer_collect_draw_meshes(Renderer *const renderer, const Vec3 eye,
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
      const size_t capacity = renderer->drawMeshCapacity ? renderer->drawMeshCapacity * 2 : 64;
      size_t *const indices = capacity < renderer->drawMeshCapacity || capacity > SIZE_MAX / sizeof(size_t) ?
          NULL : storage_alloc(STORAGE_DRAW, capacity * sizeof(*indices));
      if (!indices) {
        renderer->drawMeshCount = 0;
        if (p && p->enabled) p->frame.visibilityFallbacks++;
        return 0;
      }
      if (renderer->drawMeshes) memcpy(indices, renderer->drawMeshes, renderer->drawMeshCount * sizeof(*indices));
      storage_free(STORAGE_DRAW, renderer->drawMeshes, renderer->drawMeshCapacity * sizeof(*indices));
      renderer->drawMeshes = indices;
      renderer->drawMeshCapacity = capacity;
    }
    renderer->drawMeshes[renderer->drawMeshCount++] = i;
  }
  return 1;
}

void render_world(Renderer *const renderer, const View camera) {
  renderer->drawMeshCount = 0;
  if (renderer->framebufferWidth <= 0 || renderer->framebufferHeight <= 0)
    return;

  glUseProgram(renderer->program);

  mat4 proj, view, mvp;
  glUniform4f(renderer->blockGridLocation, renderer->worldOrigin.x, renderer->worldOrigin.y,
              renderer->worldOrigin.z, renderer->blockSize > 0.0f ? 1.0f / renderer->blockSize : 1.0f);
  const float aspectRatio =
      (float)renderer->framebufferWidth / (float)renderer->framebufferHeight;
  const float viewDistance = renderer->viewDistance > 0 ? renderer->viewDistance : DEFAULT_VIEW_DISTANCE;
  mat4_perspective(75.0f * (3.14159f / 180.0f), aspectRatio, 0.05f, viewDistance, proj);
  const Vec3 cameraPosition = camera.eye;
  const Vec3 target = vec3_add(cameraPosition, camera.front);
  mat4_lookat(cameraPosition, target, camera.up, view);
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
        renderer->streamed ? renderer->visibleMeshCount : renderer->meshCount;
    for (size_t n = 0; n < count; n++) {
      const size_t i = collected ? renderer->drawMeshes[n] :
          renderer->streamed ? renderer->visibleMeshes[n] : n;
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


void renderer_set_viewport(Renderer *const renderer, const int width, const int height) {
  renderer->framebufferWidth = width;
  renderer->framebufferHeight = height;
  glViewport(0, 0, width, height);
}

float renderer_view_distance(const Renderer *const renderer) {
  return renderer->viewDistance;
}

void renderer_set_view_distance(Renderer *const renderer, const float distance) {
  renderer->viewDistance = distance;
}

size_t renderer_neighborhood_slots(const Renderer *const renderer) {
  return renderer->visibleMeshCount;
}

UiDrawContext renderer_ui_context(const Renderer *const renderer) {
  return (UiDrawContext){.owner = renderer,
      .framebufferWidth = renderer->framebufferWidth,
      .framebufferHeight = renderer->framebufferHeight};
}

UiDrawState ui_draw_begin(const UiDrawContext *const draw, const UiDrawMode mode) {
  const Renderer *const renderer = draw->owner;
  const int menu = mode == UI_DRAW_MENU;
  const UiDrawState state = {.depthEnabled = menu ? glIsEnabled(GL_DEPTH_TEST) : 0,
      .cullEnabled = menu ? glIsEnabled(GL_CULL_FACE) : 0, .menu = menu};
  if (menu) {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
  }
  if (mode != UI_DRAW_OVERLAY) {
    mat4 identity;
    mat4_identity(identity);
    glUseProgram(renderer->program);
    glUniformMatrix4fv(renderer->mvpLocation, 1, GL_FALSE, identity);
    glBindVertexArray(renderer->textVao);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->textVbo);
  }
  if (!menu) glDisable(GL_DEPTH_TEST);
  glUniform4f(renderer->blockGridLocation, 0.0f, 0.0f, 0.0f, 0.0f);
  return state;
}

void ui_draw_vertices(const UiDrawContext *const draw, const float *const vertices,
                      const int count, const Vec3 color) {
  const Renderer *const renderer = draw->owner;
  glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)((size_t)count * 3 * sizeof(float)), vertices);
  glUniform3f(renderer->colorLocation, color.x, color.y, color.z);
  glDrawArrays(GL_TRIANGLES, 0, count);
}

void ui_draw_end(const UiDrawState state) {
  if (!state.menu || state.depthEnabled) glEnable(GL_DEPTH_TEST);
  if (state.menu && state.cullEnabled) glEnable(GL_CULL_FACE);
}
