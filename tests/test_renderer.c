/* Exercise the private renderer in a real GL context without adding test hooks
   or development controls to the game. */
#define main bwildering_application_main
#include "../main.c"
#undef main
#include <assert.h>

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
  for (int frame = 0; frame < 60; frame++) {
    update_player(&player, (PlayerInput){.strafe = 1, .forward = 1},
                   1.0f / 60, &world);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    render_world(renderer, &player);
    assert(glGetError() == GL_NO_ERROR);
  }
  assert(fabsf(player.position.x + 0.25f) < 0.0001f);
  assert(player.position.z < -0.8f && player.grounded);
  float peak = 0;
  for (int frame = 0; frame < 60; frame++) {
    update_player(&player, (PlayerInput){.jump = frame == 0}, 1.0f / 60, &world);
    peak = fmaxf(peak, player.position.y);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    render_world(renderer, &player);
    assert(glGetError() == GL_NO_ERROR);
  }
  glFinish();
  assert(peak > 1.15f && peak < 1.35f);
  assert(player.grounded && fabsf(player.position.y) < 0.0001f);
  puts("Rendered wall sliding and jump/landing checks passed.");
  renderer_destroy(renderer);
  world_destroy(&world);
}

int main(void) {
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
  assert(vertex_count(left) == 0 && vertex_count(right) == 0);
  assert(left->vao == 0 && right->vao == 0);
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
  printf("GL renderer: %s; startup %.3f ms; frame %.3f ms\n", glGetString(GL_RENDERER),
         startup, 1000 * (glfwGetTime() - start) / 120);
  unsigned char pixels[640 * 480 * 3];
  glReadPixels(0, 0, 640, 480, GL_RGB, GL_UNSIGNED_BYTE, pixels);
  size_t colored = 0;
  for (size_t i = 0; i < sizeof(pixels); i++)
    colored += pixels[i] != 0;
  assert(colored > 640 * 480); /* Scene contains substantial rendered terrain. */
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
  assert(glGetError() == GL_NO_ERROR);
  glfwDestroyWindow(window);
  glfwTerminate();
  /* Exercise the actual application startup, frame path, and shutdown as well. */
  assert(glfwInit());
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  Application application;
  assert(application_init(&application));
  assert(application.world.voxelCount > 0 && application.renderer.meshCount > 0);
  assert(application.world.blocksPerSide == PLATFORM_BLOCKS_PER_SIDE);
  float spawnY = world_surface_height(&application.world, application.player.position.x,
                                      application.player.position.z);
  assert(isfinite(spawnY) && application.player.position.y == spawnY);
  for (int i = 0; i < 60; i++)
    update_player(&application.player, (PlayerInput){0}, 1.0f / 60, &application.world);
  assert(application.player.grounded && application.player.position.y == spawnY);
  update_player(&application.player, (PlayerInput){.jump = 1}, 1.0f / 60, &application.world);
  assert(!application.player.grounded && application.player.position.y > spawnY);
  printf("Application spawn surface: %.1f; grounded and jump checks passed.\n", spawnY);
  for (int frame = 0; frame < 3; frame++) {
    application_update(&application);
    application_render(&application);
    assert(glGetError() == GL_NO_ERROR);
  }
  application_shutdown(&application);
  assert(!application.world.chunks && !application.renderer.meshes);
  puts("All renderer and application smoke tests passed.");
  return 0;
}
