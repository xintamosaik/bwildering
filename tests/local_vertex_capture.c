/* Opt-in representation experiment. Production meshing/upload/drawing is unchanged. */
#define main bwildering_application_main
#include "../main.c"
#undef main
#include <assert.h>

enum { WIDTH = 320, HEIGHT = 240, MESH_CAPACITY = 4096, MODES = 5, SAMPLES = 60 };
enum { ABSOLUTE, LOCAL, COMPACT, RELATIVE, REFERENCE };
typedef struct { uint8_t x, y, z; } CompactVertex;
typedef struct {
  SectionGpuMesh gpu;
  Vec3 origin;
  double worldOrigin[3];
} TrialMesh;
typedef struct {
  GLuint program;
  GLint mvp, color, origin, scale;
} TrialProgram;
static TrialMesh meshes[MODES][MESH_CAPACITY];
static TrialProgram programs[MODES];
static SurfaceVertex grid[SECTION_MESH_MAX_VERTICES];
static SurfaceVertex absolute[SECTION_MESH_MAX_VERTICES];
static SurfaceVertex local[SECTION_MESH_MAX_VERTICES];
static CompactVertex compact[SECTION_MESH_MAX_VERTICES];
static unsigned char readback[SECTION_MESH_MAX_VERTICES * sizeof(SurfaceVertex)];
static float depths[MODES][WIDTH * HEIGHT];
static size_t meshCount, vertices, visibleCount;
static size_t visible[MESH_CAPACITY];
static double uploads[MODES], encoding[MODES];
static const char *const names[MODES] = {"absolute", "local-float", "local-u8", "relative-u8", "reference"};

static TrialProgram trial_program(const int mode) {
  if (mode == ABSOLUTE || mode == REFERENCE) {
    const GLuint p = create_shader_program();
    assert(p);
    return (TrialProgram){p, glGetUniformLocation(p, "mvp"), glGetUniformLocation(p, "color"), -1, -1};
  }
  const char *const floatSource =
      "#version 330 core\nlayout(location=0) in vec3 position;\n"
      "uniform mat4 mvp; uniform vec3 origin; uniform float scale; out vec3 worldPosition;\n"
      "void main(){worldPosition=origin+position*scale;gl_Position=mvp*vec4(worldPosition,1);}\n";
  const char *const integerSource =
      "#version 330 core\nlayout(location=0) in uvec3 position;\n"
      "uniform mat4 mvp; uniform vec3 origin; uniform float scale; out vec3 worldPosition;\n"
      "void main(){worldPosition=origin+vec3(position)*scale;gl_Position=mvp*vec4(worldPosition,1);}\n";
  /* Match production lighting, including derivatives and front-face handling. */
  const char *const fragmentSource =
      "#version 330 core\nout vec4 FragColor; uniform vec3 color; in vec3 worldPosition;\n"
      "void main(){vec3 normal=normalize(cross(dFdx(worldPosition),dFdy(worldPosition)));"
      "if(!gl_FrontFacing)normal=-normal;"
      "float light=0.45+0.55*max(dot(normal,normalize(vec3(-0.4,1,0.3))),0);"
      "FragColor=vec4(color*light,1);}\n";
  const GLuint vs = compile_shader(GL_VERTEX_SHADER, mode == LOCAL ? floatSource : integerSource);
  const GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fragmentSource);
  assert(vs && fs);
  const GLuint p = glCreateProgram();
  glAttachShader(p, vs); glAttachShader(p, fs); glLinkProgram(p);
  glDeleteShader(vs); glDeleteShader(fs);
  GLint success = 0;
  glGetProgramiv(p, GL_LINK_STATUS, &success);
  assert(success);
  return (TrialProgram){p, glGetUniformLocation(p, "mvp"), glGetUniformLocation(p, "color"),
                        glGetUniformLocation(p, "origin"), glGetUniformLocation(p, "scale")};
}

static Vec3 section_origin(const World *const world, const int x, const int y, const int z,
                           const Vec3 eye, const int relative) {
  /* Subtract in double before narrowing; absolute shader translations still cancel. */
  return (Vec3){(float)((double)world->origin.x + (double)(x * SECTION_SIZE) * (double)world->blockSize - (relative ? (double)eye.x : 0.0)),
                (float)((double)world->origin.y + (double)(y * SECTION_SIZE) * (double)world->blockSize - (relative ? (double)eye.y : 0.0)),
                (float)((double)world->origin.z + (double)(z * SECTION_SIZE) * (double)world->blockSize - (relative ? (double)eye.z : 0.0))};
}

static void load_meshes(const World *const world, const Vec3 eye) {
  meshCount = vertices = 0;
  memset(uploads, 0, sizeof(uploads));
  memset(encoding, 0, sizeof(encoding));
  /* A borrowed view changes only the render transform, never world ownership. */
  World canonical = *world;
  canonical.origin = (Vec3){0}; canonical.blockSize = 1.0f;
  for (size_t c = 0; c < world->chunkCount; c++)
    for (const Subchunk *s = world->chunks[c].sections; s; s = s->next) {
      const int x = world->chunks[c].x, y = s->sectionY, z = world->chunks[c].z;
      SectionMesh source = {0}, baseline = {0};
      assert(section_mesh_build_scratch(&canonical, x, y, z, grid, SECTION_MESH_MAX_VERTICES, &source));
      if (!source.vertexCount) continue;
      assert(meshCount < MESH_CAPACITY);
      assert(section_mesh_build_scratch(world, x, y, z, absolute, SECTION_MESH_MAX_VERTICES, &baseline));
      assert(source.vertexCount == baseline.vertexCount);
      assert(!memcmp(source.firstVertex, baseline.firstVertex, sizeof(source.firstVertex)));
      assert(!memcmp(source.typeVertexCount, baseline.typeVertexCount, sizeof(source.typeVertexCount)));
      const double encodeStart = glfwGetTime();
      for (size_t v = 0; v < source.vertexCount; v++) {
        local[v] = (SurfaceVertex){grid[v].x - (float)(x * SECTION_SIZE),
                                   grid[v].y - (float)(y * SECTION_SIZE),
                                   grid[v].z - (float)(z * SECTION_SIZE)};
        assert(local[v].x >= 0.0f && local[v].x <= 16.0f && floorf(local[v].x) == local[v].x);
        assert(local[v].y >= 0.0f && local[v].y <= 16.0f && floorf(local[v].y) == local[v].y);
        assert(local[v].z >= 0.0f && local[v].z <= 16.0f && floorf(local[v].z) == local[v].z);
        /* Corner 16 is inclusive. No normalized mapping or signed-byte ambiguity. */
        compact[v] = (CompactVertex){(uint8_t)local[v].x, (uint8_t)local[v].y, (uint8_t)local[v].z};
        assert((float)compact[v].x + (float)(x * SECTION_SIZE) == grid[v].x);
        assert((float)compact[v].y + (float)(y * SECTION_SIZE) == grid[v].y);
        assert((float)compact[v].z + (float)(z * SECTION_SIZE) == grid[v].z);
        assert(absolute[v].x == world->origin.x + grid[v].x * world->blockSize);
        assert(absolute[v].y == world->origin.y + grid[v].y * world->blockSize);
        assert(absolute[v].z == world->origin.z + grid[v].z * world->blockSize);
      }
      const double encodeSeconds = glfwGetTime() - encodeStart;
      /* Combined float+integer encoding and assertions; diagnostic, not mesher cost. */
      encoding[LOCAL] += encodeSeconds;
      for (int slot = 0; slot < MODES; slot++) {
        const int mode = (slot + (int)(meshCount % MODES)) % MODES;
        TrialMesh *const trial = &meshes[mode][meshCount];
        trial->origin = section_origin(world, x, y, z, eye, mode == RELATIVE);
        trial->worldOrigin[0] = (double)world->origin.x + (double)(x * SECTION_SIZE) * (double)world->blockSize;
        trial->worldOrigin[1] = (double)world->origin.y + (double)(y * SECTION_SIZE) * (double)world->blockSize;
        trial->worldOrigin[2] = (double)world->origin.z + (double)(z * SECTION_SIZE) * (double)world->blockSize;
        SectionGpuMesh *const gpu = &trial->gpu;
        *gpu = (SectionGpuMesh){.chunkX=x, .sectionY=y, .chunkZ=z};
        for (int t = 0; t < BLOCK_TYPE_COUNT; t++) {
          assert(source.firstVertex[t] <= INT_MAX && source.typeVertexCount[t] <= INT_MAX);
          gpu->firstVertex[t] = (GLint)source.firstVertex[t];
          gpu->vertexCount[t] = (GLsizei)source.typeVertexCount[t];
        }
        gpu->boundsMin = gpu->boundsMax = (Vec3){absolute[0].x, absolute[0].y, absolute[0].z};
        for (size_t v = 0; v < source.vertexCount; v++) {
          gpu->boundsMin.x = fminf(gpu->boundsMin.x, absolute[v].x);
          gpu->boundsMin.y = fminf(gpu->boundsMin.y, absolute[v].y);
          gpu->boundsMin.z = fminf(gpu->boundsMin.z, absolute[v].z);
          gpu->boundsMax.x = fmaxf(gpu->boundsMax.x, absolute[v].x);
          gpu->boundsMax.y = fmaxf(gpu->boundsMax.y, absolute[v].y);
          gpu->boundsMax.z = fmaxf(gpu->boundsMax.z, absolute[v].z);
        }
        glGenVertexArrays(1, &gpu->vao); glGenBuffers(1, &gpu->vbo);
        glBindVertexArray(gpu->vao); glBindBuffer(GL_ARRAY_BUFFER, gpu->vbo);
        if (mode == REFERENCE) {
          /* Independent CPU double reconstruction, already relative to the eye. */
          for (size_t v = 0; v < source.vertexCount; v++)
            grid[v] = (SurfaceVertex){
                (float)((double)world->origin.x + (double)(x * SECTION_SIZE + compact[v].x) * (double)world->blockSize - (double)eye.x),
                (float)((double)world->origin.y + (double)(y * SECTION_SIZE + compact[v].y) * (double)world->blockSize - (double)eye.y),
                (float)((double)world->origin.z + (double)(z * SECTION_SIZE + compact[v].z) * (double)world->blockSize - (double)eye.z)};
        }
        const int integer = mode == COMPACT || mode == RELATIVE;
        const size_t stride = integer ? sizeof(CompactVertex) : sizeof(SurfaceVertex);
        const size_t bytes = source.vertexCount * stride;
        const void *const data = integer ? (const void *)compact :
            mode == ABSOLUTE ? (const void *)absolute : mode == REFERENCE ? (const void *)grid : (const void *)local;
        glFinish(); /* Drain outside CPU upload submission timing. */
        const double start = glfwGetTime();
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)bytes, data, GL_STATIC_DRAW);
        if (integer) glVertexAttribIPointer(0, 3, GL_UNSIGNED_BYTE, (GLsizei)stride, (void *)0);
        else glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, (GLsizei)stride, (void *)0);
        glEnableVertexAttribArray(0);
        uploads[mode] += glfwGetTime() - start;
        GLint actualBytes = 0;
        glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &actualBytes);
        assert((size_t)actualBytes == bytes);
        glGetBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)bytes, readback);
        assert(!memcmp(data, readback, bytes));
        assert(glGetError() == GL_NO_ERROR);
      }
      vertices += source.vertexCount;
      meshCount++;
    }
}

static void matrices(const Vec3 eye, const Vec3 front, const float distance, mat4 absoluteMvp, mat4 relativeMvp) {
  mat4 projection, view;
  mat4_perspective(75.0f * (3.14159f / 180.0f), (float)WIDTH / (float)HEIGHT, 0.05f, distance, projection);
  mat4_lookat(eye, vec3_add(eye, front), (Vec3){0, 1, 0}, view);
  mat4_multiply(projection, view, absoluteMvp);
  mat4_lookat((Vec3){0}, front, (Vec3){0, 1, 0}, view);
  mat4_multiply(projection, view, relativeMvp);
}

static size_t draw(const int mode, const mat4 absoluteMvp, const mat4 relativeMvp,
                   const Vec3 eye, const float scale, const int cull) {
  const TrialProgram p = programs[mode];
  glUseProgram(p.program);
  glUniformMatrix4fv(p.mvp, 1, GL_FALSE, mode >= RELATIVE ? relativeMvp : absoluteMvp);
  if (p.scale >= 0) glUniform1f(p.scale, scale);
  size_t draws = 0;
  /* Preserve production material-first ordering; each origin update costs a uniform per draw. */
  for (int t = 0; t < BLOCK_TYPE_COUNT; t++) {
    Vec3 color;
    assert(block_type_color((BlockType)t, &color));
    glUniform3f(p.color, color.x, color.y, color.z);
    for (size_t n = 0; n < (cull ? visibleCount : meshCount); n++) {
      const size_t i = cull ? visible[n] : n;
      const TrialMesh *const trial = &meshes[mode][i];
      const SectionGpuMesh *const gpu = &trial->gpu;
      if (!gpu->vertexCount[t]) continue;
      if (mode == RELATIVE)
        glUniform3f(p.origin, (float)(trial->worldOrigin[0] - (double)eye.x),
                    (float)(trial->worldOrigin[1] - (double)eye.y),
                    (float)(trial->worldOrigin[2] - (double)eye.z));
      else if (p.origin >= 0) glUniform3f(p.origin, trial->origin.x, trial->origin.y, trial->origin.z);
      glBindVertexArray(gpu->vao);
      glDrawArrays(GL_TRIANGLES, gpu->firstVertex[t], gpu->vertexCount[t]);
      draws++;
    }
  }
  return draws;
}

static int compare_double(const void *const a, const void *const b) {
  const double x = *(const double *)a, y = *(const double *)b;
  return (x > y) - (x < y);
}

static void evaluate(const char *const scene, const World *const world, const Vec3 eye,
                      const Vec3 front, const float distance, const int precision) {
  load_meshes(world, eye);
  mat4 absoluteMvp, relativeMvp;
  matrices(eye, front, distance, absoluteMvp, relativeMvp);
  visibleCount = 0;
  for (size_t i = 0; i < meshCount; i++)
    if (section_in_frustum(&meshes[ABSOLUTE][i].gpu, absoluteMvp) &&
        section_within_view_distance(&meshes[ABSOLUTE][i].gpu, eye, distance)) visible[visibleCount++] = i;
  size_t draws[MODES] = {0}, culledDraws[MODES] = {0};
  for (int mode = 0; mode < MODES; mode++) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    draws[mode] = draw(mode, absoluteMvp, relativeMvp, eye, world->blockSize, 0);
    glReadPixels(0, 0, WIDTH, HEIGHT, GL_DEPTH_COMPONENT, GL_FLOAT, depths[mode]);
    assert(draws[mode] == draws[ABSOLUTE]);
  }
  for (int mode = 0; mode < MODES - 1; mode++) {
    size_t coverageDiff = 0, depthDiff = 0, covered = 0;
    float maxError = 0.0f;
    for (size_t i = 0; i < WIDTH * HEIGHT; i++) {
      covered += depths[REFERENCE][i] < 1.0f;
      coverageDiff += (depths[mode][i] < 1.0f) != (depths[REFERENCE][i] < 1.0f);
      const float error = fabsf(depths[mode][i] - depths[REFERENCE][i]);
      depthDiff += error > 0.00001f;
      maxError = fmaxf(maxError, error);
    }
    assert(covered > 100);
    printf("precision,%s,%s,%zu,%zu,%zu,%.9g\n", scene, names[mode], covered, coverageDiff, depthDiff, (double)maxError);
    if (mode == RELATIVE) assert(!coverageDiff && !depthDiff);
    if (mode == LOCAL || mode == COMPACT)
      for (size_t i = 0; i < WIDTH * HEIGHT; i++) {
        assert((depths[mode][i] < 1.0f) == (depths[ABSOLUTE][i] < 1.0f));
        assert(fabsf(depths[mode][i] - depths[ABSOLUTE][i]) <= 0.000001f);
      }
    /* Hardware clipping is the independent reference for the existing bounds predicates. */
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    const size_t culled = draw(mode, absoluteMvp, relativeMvp, eye, world->blockSize, 1);
    culledDraws[mode] = culled;
    assert(culled == culledDraws[ABSOLUTE]);
    glReadPixels(0, 0, WIDTH, HEIGHT, GL_DEPTH_COMPONENT, GL_FLOAT, depths[REFERENCE]);
    size_t cullDiff = 0;
    for (size_t i = 0; i < WIDTH * HEIGHT; i++) cullDiff += fabsf(depths[mode][i] - depths[REFERENCE][i]) > 0.00001f;
    assert(!cullDiff);
    printf("culling,%s,%s,%zu,%zu,%zu\n", scene, names[mode], draws[mode], culled, cullDiff);
    /* Restore the double reference overwritten by culling readback. */
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    draw(REFERENCE, absoluteMvp, relativeMvp, eye, world->blockSize, 0);
    glReadPixels(0, 0, WIDTH, HEIGHT, GL_DEPTH_COMPONENT, GL_FLOAT, depths[REFERENCE]);
  }
  if (!precision) {
    double cpu[4][SAMPLES], gpu[4][SAMPLES];
    GLuint query;
    glGenQueries(1, &query);
    GLint timerBits = 0;
    glGetQueryiv(GL_TIME_ELAPSED, GL_QUERY_COUNTER_BITS, &timerBits);
    for (int frame = 0; frame < SAMPLES + 4; frame++)
      for (int slot = 0; slot < 4; slot++) {
        const int mode = (frame + slot) % 4;
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glFinish();
        if (timerBits) glBeginQuery(GL_TIME_ELAPSED, query);
        const double start = glfwGetTime();
        const size_t submitted = draw(mode, absoluteMvp, relativeMvp, eye, world->blockSize, 1);
        const double elapsed = glfwGetTime() - start;
        if (timerBits) glEndQuery(GL_TIME_ELAPSED);
        GLuint64 ns = 0;
        if (timerBits) glGetQueryObjectui64v(query, GL_QUERY_RESULT, &ns);
        else glFinish();
        assert(submitted > 0 && submitted == culledDraws[mode] && glGetError() == GL_NO_ERROR);
        if (frame >= 4) {
          cpu[mode][frame - 4] = elapsed * 1000.0;
          gpu[mode][frame - 4] = (double)ns / 1e6;
        }
      }
    glDeleteQueries(1, &query);
    for (int mode = 0; mode < 4; mode++) {
      qsort(cpu[mode], SAMPLES, sizeof(double), compare_double);
      qsort(gpu[mode], SAMPLES, sizeof(double), compare_double);
      const size_t stride = mode >= COMPACT ? sizeof(CompactVertex) : sizeof(SurfaceVertex);
      printf("timing,%s,%s,%zu,%zu,%zu,%.6f,%.6f,%.6f,%.6f,%.6f,%d,%zu,%zu\n", scene, names[mode],
             meshCount, vertices, vertices * stride, uploads[mode] * 1000.0,
             cpu[mode][SAMPLES / 2], cpu[mode][SAMPLES * 95 / 100],
             gpu[mode][SAMPLES / 2], gpu[mode][SAMPLES * 95 / 100], timerBits, culledDraws[mode], mode == ABSOLUTE ? 0 : culledDraws[mode]);
    }
    printf("encoding,%s,combined_float_u8_with_assertions_ms,%.6f\n", scene, encoding[LOCAL] * 1000.0);
  }
  for (int mode = 0; mode < MODES; mode++)
    for (size_t i = 0; i < meshCount; i++) section_gpu_mesh_destroy(&meshes[mode][i].gpu);
}

int main(void) {
  assert(sizeof(CompactVertex) == 3);
  glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
  assert(glfwInit());
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  GLFWwindow *const window = glfwCreateWindow(WIDTH, HEIGHT, "Local vertex evaluation", NULL, NULL);
  assert(window); glfwMakeContextCurrent(window);
  glewExperimental = GL_TRUE;
  assert(glewInit() == GLEW_OK);
  while (glGetError() != GL_NO_ERROR) {}
  glfwSwapInterval(0); glEnable(GL_DEPTH_TEST); glEnable(GL_CULL_FACE);
  glViewport(0, 0, WIDTH, HEIGHT);
  printf("device,compiler=%s,renderer=%s,version=%s,seed=%u,resolution=%dx%d,samples=%d\n",
         __VERSION__, (const char *)glGetString(GL_RENDERER), (const char *)glGetString(GL_VERSION), WORLD_SEED, WIDTH, HEIGHT, SAMPLES);
  puts("precision,scene,mode,reference_covered,coverage_diff,depth_diff_gt_1e-5,max_depth_error");
  puts("culling,scene,mode,unculled_draws,culled_draws,depth_diff_gt_1e-5");
  puts("timing,scene,mode,sections,vertices,payload_bytes,upload_submit_total_ms,draw_cpu_p50_ms,draw_cpu_p95_ms,gpu_p50_ms,gpu_p95_ms,timer_bits,draws,origin_updates");
  for (int mode = 0; mode < MODES; mode++) programs[mode] = trial_program(mode);
  const Vec3 front = {0.0f, -0.4472136f, -0.8944272f};
  World world = {0};
  /* Find a deterministic mountainous region, independent of terrain format hashes. */
  int peakX = 0, peakZ = 0, peakY = TERRAIN_BASE_Y;
  for (int x = -1024; x <= 1024; x += 64)
    for (int z = -1024; z <= 1024; z += 64) {
      const int h = terrain_sample_column(WORLD_SEED, x, z).height;
      if (h > peakY) { peakX = x; peakZ = z; peakY = h; }
    }
  assert(create_terrain_region(&world, peakX - 64, peakZ - 64, 128, (Vec3){0}, 1.0f, WORLD_SEED));
  const Vec3 mountainEye = {(float)peakX + 0.5f, (float)peakY + 32.0f, (float)peakZ + 48.5f};
  evaluate("mountains", &world, mountainEye, front, 300.0f, 0);
  /* Thousands of holes and additions, multiple materials, crossing section boundaries. */
  for (int x = peakX - 32; x < peakX + 32; x += 2)
    for (int z = peakZ - 32; z < peakZ + 32; z += 2) {
      const int h = terrain_sample_column(WORLD_SEED, x, z).height;
      assert(world_set_block(&world, x, h - 1, z, BLOCK_TYPE_EMPTY));
      assert(world_set_block(&world, x, h + 1, z, (BlockType)((x - peakX + z - peakZ + 128) % BLOCK_TYPE_COUNT)));
    }
  evaluate("edited", &world, mountainEye, front, 300.0f, 0);
  world_destroy(&world);
  assert(world_init_region(&world, -64, -64, 128, (Vec3){0}, 1.0f));
  for (int x = -64; x < 64; x++) for (int z = -64; z < 64; z++)
    assert(world_set_block(&world, x, -1, z, x < 0 ? BLOCK_TYPE_STONE : BLOCK_TYPE_SNOW));
  evaluate("flat", &world, (Vec3){0.5f, 16.0f, 48.5f}, front, 300.0f, 0);
  world_destroy(&world);
  const int positions[] = {0, WORLD_COORD_MIN + 31, WORLD_COORD_MAX - 62, -32};
  const Vec3 origins[] = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {-57.25f, 31.5f, -83.0f}};
  const float scales[] = {1.0f, 1.0f, 1.0f, 2.5f};
  const char *const fixtures[] = {"near-seams", "distant-negative", "distant-positive", "translated-scaled"};
  for (int fixture = 0; fixture < 4; fixture++) {
    const int start = positions[fixture];
    assert(world_init_region(&world, start, start, 32, origins[fixture], scales[fixture]));
    for (int x = start; x < start + 32; x++) for (int z = start; z < start + 32; z++)
      for (int y = start - 1; y <= start; y++)
        assert(world_set_block(&world, x, y, z, (BlockType)((x - start) % BLOCK_TYPE_COUNT)));
    const Vec3 baseEye = {(float)start + 15.5f, (float)start + 10.25f, (float)start + 35.5f};
    const Vec3 eye = vec3_add(origins[fixture], vec3_scale(baseEye, scales[fixture]));
    /* Two actual camera positions plus an edit: report errors, do not assume jitter. */
    for (int pose = 0; pose < 2; pose++) {
      char label[80];
      snprintf(label, sizeof(label), "%s-%d", fixtures[fixture], pose);
      evaluate(label, &world, vec3_add(eye, (Vec3){(float)pose * 0.125f * scales[fixture], 0, 0}),
               front, 128.0f * scales[fixture], 1);
      assert(world_set_block(&world, start + 15, start, start + 15, BLOCK_TYPE_EMPTY));
    }
    world_destroy(&world);
  }
  for (int mode = 0; mode < MODES; mode++) glDeleteProgram(programs[mode].program);
  assert(glGetError() == GL_NO_ERROR);
  glfwDestroyWindow(window); glfwTerminate();
  return 0;
}
