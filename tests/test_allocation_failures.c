#include "../game_logic.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Linker wrappers allow deterministic failure at every allocation without
   shipping allocator hooks in the world or renderer. */
void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
static int fail_after = -1;
static size_t allocation_calls;
static int reject_allocation(void) {
  if (fail_after < 0)
    return 0;
  if (fail_after == 0)
    return 1;
  fail_after--;
  return 0;
}
void *__wrap_malloc(size_t size) {
  allocation_calls++;
  return reject_allocation() ? NULL : __real_malloc(size);
}
void *__wrap_calloc(size_t count, size_t size) {
  allocation_calls++;
  return reject_allocation() ? NULL : __real_calloc(count, size);
}

static int generate_world(int generator, World *world) {
  if (generator == 0)
    return create_platform(world, 17, (Vec3){12, -3, 8}, 2, 1);
  if (generator == 1)
    return create_terrain(world, 17, (Vec3){12, -3, 8}, 2, 1);
  return create_terrain_region(world, -17, 15, 17, (Vec3){12, -3, 8}, 2, 1);
}

static void assert_world_equal(const World *actual, const World *expected) {
  assert(actual->minX == expected->minX && actual->minZ == expected->minZ);
  assert(actual->blocksPerSide == expected->blocksPerSide);
  assert(actual->blockSize == expected->blockSize);
  assert(actual->origin.x == expected->origin.x && actual->origin.y == expected->origin.y &&
         actual->origin.z == expected->origin.z);
  for (int x = expected->minX; x < expected->minX + expected->blocksPerSide; x++)
    for (int z = expected->minZ; z < expected->minZ + expected->blocksPerSide; z++) {
      float wx = expected->origin.x + (x + 0.5f) * expected->blockSize;
      float wz = expected->origin.z + (z + 0.5f) * expected->blockSize;
      float surface = world_surface_height(expected, wx, wz);
      float actualSurface = world_surface_height(actual, wx, wz);
      assert(isnan(surface) ? isnan(actualSurface) : actualSurface == surface);
      for (int y = WORLD_MIN_SECTION_Y * SECTION_SIZE;
           y < (WORLD_MIN_SECTION_Y + WORLD_SECTION_COUNT) * SECTION_SIZE; y++)
        assert(world_get_block(actual, x, y, z) == world_get_block(expected, x, y, z));
    }
}

static int evict_prepared;

static void prepare_edited_stream(World *world) {
  assert(create_terrain_streamed(world, 33, (Vec3){0}, 1, 1));
  /* Existing data in both the partially generated chunk and its neighbour. */
  int top = terrain_sample_column(1, 0, 0).height - 1;
  assert(world_set_block(world, 0, top, 0, BLOCK_TYPE_EMPTY));
  assert(world_set_block(world, 0, 400, 0, BLOCK_TYPE_SNOW));
  /* A separate column exposes removed surface blocks rather than the addition.
     Its deep edit leaves intervening sections procedural until materialization. */
  int height = terrain_sample_column(1, 1, 0).height;
  for (int y = height - 1; y >= height - 3; y--)
    assert(world_set_block(world, 1, y, 0, BLOCK_TYPE_EMPTY));
  assert(world_set_block(world, 1, TERRAIN_BASE_Y + 1, 0, BLOCK_TYPE_EMPTY));
  assert(world_set_block(world, 0, -400, 0, BLOCK_TYPE_WATER));
  assert(world_materialize_chunk(world, -1, 0));
  if (evict_prepared) world_release_chunk(world, 0, 0);
}

static void test_materialization_preserves_existing_ownership(void) {
  World reference = {0};
  prepare_edited_stream(&reference);
  size_t before = allocation_calls;
  assert(world_materialize_chunk(&reference, 0, 0));
  size_t allocations = allocation_calls - before;
  assert(allocations > 0);
  for (size_t failure = 0; failure < allocations; failure++) {
    World world = {0};
    prepare_edited_stream(&world);
    World original = world; /* Borrowed snapshot, never destroyed. */
    static VoxelEdit savedEdits[WORLD_EDIT_CAPACITY];
    memcpy(savedEdits, world.edits, world.editUsed * sizeof(*world.edits));
    Chunk *chunks = malloc(world.chunkCount * sizeof(*chunks));
    Subchunk *sections = malloc(world.sectionCount * sizeof(*sections));
    uint8_t *payloads = malloc(world.sectionCount * SECTION_VOLUME);
    assert(chunks && sections && payloads);
    memcpy(chunks, world.chunks, world.chunkCount * sizeof(*chunks));
    size_t n = 0;
    for (size_t c = 0; c < world.chunkCount; c++)
      for (const Subchunk *s = world.chunks[c].sections; s; s = s->next)
        {
          sections[n] = *s;
          for (size_t b = 0; b < SECTION_VOLUME; b++)
            payloads[n * SECTION_VOLUME + b] = (uint8_t)section_block(s, b);
          n++;
        }
    fail_after = (int)failure;
    assert(!world_materialize_chunk(&world, 0, 0));
    fail_after = -1;
    assert(world.chunks == original.chunks);
    assert(world.edits == original.edits && world.editCount == original.editCount);
    assert(world.editUsed == original.editUsed && world.editFree == original.editFree);
    assert(!memcmp(savedEdits, world.edits, world.editUsed * sizeof(*world.edits)));
    assert(world.sectionCount == original.sectionCount && world.voxelCount == original.voxelCount);
    assert(world.hasMaterializedBounds == original.hasMaterializedBounds);
    assert(world.minMaterializedChunkX == original.minMaterializedChunkX);
    assert(world.maxMaterializedChunkX == original.maxMaterializedChunkX);
    assert(world.minMaterializedChunkZ == original.minMaterializedChunkZ);
    assert(world.maxMaterializedChunkZ == original.maxMaterializedChunkZ);
    n = 0;
    for (size_t c = 0; c < world.chunkCount; c++) {
      assert(world.chunks[c].sections == chunks[c].sections);
      assert(world.chunks[c].terrainReady == chunks[c].terrainReady);
      assert(world.chunks[c].edited == chunks[c].edited);
      assert(world.chunks[c].editHead == chunks[c].editHead);
      for (const Subchunk *s = world.chunks[c].sections; s; s = s->next) {
        const Subchunk *saved = &sections[n++];
        assert(s->next == saved->next && s->sectionY == saved->sectionY);
        assert(s->dirty == saved->dirty && s->meshCandidate == saved->meshCandidate);
        assert(s->voxelCount == saved->voxelCount);
        assert(s->storage == saved->storage && s->material == saved->material);
        assert(s->blocks == saved->blocks);
        for (size_t b = 0; b < SECTION_VOLUME; b++)
          assert(section_block(s, b) == payloads[(n - 1) * SECTION_VOLUME + b]);
      }
    }
    assert_world_equal(&world, &reference);
    assert(world_materialize_chunk(&world, 0, 0));
    assert_world_equal(&world, &reference);
    free(chunks);
    free(sections);
    free(payloads);
    world_destroy(&world);
  }
  world_destroy(&reference);
}

static void test_regenerated_mesh_failure_retry(void) {
  World world = {0};
  assert(create_terrain_streamed(&world, 33, (Vec3){0}, 1, 1));
  int sy = voxel_section_coord(terrain_sample_column(1, 0, 0).height - 1);
  SectionMesh expected = {0}, mesh = {0};
  size_t before = allocation_calls;
  assert(section_mesh_build(&world, 0, sy, 0, &expected));
  size_t allocations = allocation_calls - before;
  assert(expected.vertexCount && allocations >= 2); /* Temporary section + vertices. */
  assert(world_materialize_chunk(&world, 0, 0));
  assert(section_mesh_build(&world, 0, sy, 0, &mesh));
  world_release_chunk(&world, 0, 0);
  for (size_t failure = 0; failure < allocations; failure++) {
    SurfaceVertex *owned = mesh.vertices;
    fail_after = (int)failure;
    assert(!section_mesh_build(&world, 0, sy, 0, &mesh));
    fail_after = -1;
    assert(mesh.vertices == owned && mesh.vertexCount == expected.vertexCount);
    assert(!memcmp(mesh.firstVertex, expected.firstVertex, sizeof(mesh.firstVertex)));
    assert(!memcmp(mesh.typeVertexCount, expected.typeVertexCount, sizeof(mesh.typeVertexCount)));
    assert(!memcmp(mesh.vertices, expected.vertices, mesh.vertexCount * sizeof(*mesh.vertices)));
    assert(!world.sectionCount && !world.voxelCount);
    assert(!world_section(&world, 0, sy, 0));
    assert(section_mesh_build(&world, 0, sy, 0, &mesh));
    assert(mesh.vertexCount == expected.vertexCount);
    assert(!memcmp(mesh.vertices, expected.vertices, mesh.vertexCount * sizeof(*mesh.vertices)));
  }
  section_mesh_destroy(&mesh);
  section_mesh_destroy(&expected);
  world_destroy(&world);
}

static void test_storage_expansion_failure(void) {
  for (int mode = 0; mode < 4; mode++) {
    World world = {0};
    assert(create_terrain_streamed(&world, 32, (Vec3){0}, 1, 1));
    if (mode < 2) assert(world_materialize_chunk(&world, 0, 0));
    int y = mode == 0 || mode == 2 ? TERRAIN_BASE_Y : 416;
    if (mode == 1) {
      assert(world_set_block(&world, 0, y, 0, BLOCK_TYPE_SNOW));
      assert(world_set_block(&world, 0, y, 0, BLOCK_TYPE_EMPTY));
    }
    Subchunk *s = world_section_mut(&world, 0, voxel_section_coord(y), 0);
    if (s) {
      assert(s->storage == (mode == 0 ? SECTION_UNIFORM : SECTION_EMPTY));
      s->dirty = s->meshCandidate = 0;
    }
    /* Neighbor flags must not change when a boundary edit fails. */
    assert(world_set_block(&world, -1, y, 0, BLOCK_TYPE_SNOW));
    Subchunk *neighbor = world_section_mut(&world, -1, voxel_section_coord(y), 0);
    neighbor->dirty = neighbor->meshCandidate = 0;
    Chunk *chunk = &world.chunks[(0 - world.minChunkX) * world.chunksPerSide - world.minChunkZ];
    int edited = chunk->edited;
    Subchunk saved = s ? *s : (Subchunk){0};
    Subchunk *head = chunk->sections;
    size_t sections = world.sectionCount, voxels = world.voxelCount;
    /* Existing uniform/empty: one payload allocation. Missing procedural/air:
       section node succeeds, payload fails on the second attempt. */
    int allocations = s ? 1 : 2;
    BlockType before[SECTION_VOLUME];
    for (int x = 0; x < 16; x++)
      for (int ly = 0; ly < 16; ly++)
        for (int z = 0; z < 16; z++)
          before[(x * 16 + ly) * 16 + z] = world_get_block(&world, x, y + ly, z);
    for (int failure = 0; failure < allocations; failure++) {
      fail_after = failure;
      assert(!world_set_block(&world, 0, y, 0, BLOCK_TYPE_GRASS));
      fail_after = -1;
      assert(world.sectionCount == sections && world.voxelCount == voxels);
      assert(chunk->sections == head && chunk->edited == edited);
      assert(!neighbor->dirty && !neighbor->meshCandidate);
      assert(world_section(&world, 0, voxel_section_coord(y), 0) == s);
      if (s) {
        assert(s->storage == saved.storage && s->material == saved.material);
        assert(s->blocks == saved.blocks && s->next == saved.next);
        assert(s->dirty == saved.dirty && s->meshCandidate == saved.meshCandidate);
        assert(s->voxelCount == saved.voxelCount && s->sectionY == saved.sectionY);
      }
      for (int x = 0; x < 16; x++)
        for (int ly = 0; ly < 16; ly++)
          for (int z = 0; z < 16; z++)
            assert(world_get_block(&world, x, y + ly, z) == before[(x * 16 + ly) * 16 + z]);
    }
    assert(world_set_block(&world, 0, y, 0, BLOCK_TYPE_GRASS));
    assert(world_get_block(&world, 0, y, 0) == BLOCK_TYPE_GRASS);
    assert(neighbor->dirty && neighbor->meshCandidate);
    world_destroy(&world);
  }
}

int main(void) {
  test_storage_expansion_failure();
  test_materialization_preserves_existing_ownership();
  evict_prepared = 1;
  test_materialization_preserves_existing_ownership();
  evict_prepared = 0;
  /* First edit reserves arena, node and dense payload; every failure is atomic. */
  for (int failure = 0; failure < 3; failure++) {
    World first = {0};
    assert(create_terrain_streamed(&first, 32, (Vec3){0}, 1, 1));
    fail_after = failure;
    assert(!world_set_block(&first, -1, 500, -1, BLOCK_TYPE_SNOW));
    fail_after = -1;
    assert(!first.edits && !first.editCount && !first.editUsed && !first.editFree);
    assert(!first.sectionCount && !first.voxelCount && !first.hasMaterializedBounds);
    for (size_t c = 0; c < first.chunkCount; c++)
      assert(!first.chunks[c].sections && !first.chunks[c].edited && !first.chunks[c].editHead);
    world_destroy(&first);
  }
  /* Replacing and restoring an unloaded authoritative entry must also roll back. */
  for (int restore = 0; restore < 2; restore++)
    for (int failure = 0; failure < 2; failure++) {
      World edited = {0};
      assert(create_terrain_streamed(&edited, 32, (Vec3){0}, 1, 1));
      assert(world_set_block(&edited, -1, 500, -1, BLOCK_TYPE_SNOW));
      world_release_chunk(&edited, -1, -1);
      const VoxelEdit original = edited.edits[0];
      const uint32_t head = edited.chunks[0].editHead;
      fail_after = failure;
      assert(!world_set_block(&edited, -1, 500, -1,
                             restore ? BLOCK_TYPE_EMPTY : BLOCK_TYPE_GRASS));
      fail_after = -1;
      assert(!edited.sectionCount && !edited.voxelCount);
      assert(edited.editCount == 1 && edited.editUsed == 1 && !edited.editFree);
      assert(edited.chunks[0].editHead == head && edited.chunks[0].edited);
      assert(!memcmp(&original, &edited.edits[0], sizeof(original)));
      assert(world_get_block(&edited, -1, 500, -1) == BLOCK_TYPE_SNOW);
      world_destroy(&edited);
    }
  test_regenerated_mesh_failure_retry();
  World world = {0};
  assert(world_init(&world, 32, (Vec3){0}, 1));
  fail_after = 0;
  assert(!world_set_block(&world, -1, 0, 0, BLOCK_TYPE_GRASS));
  assert(world.voxelCount == 0 && world.sectionCount == 0);
  assert(world_get_block(&world, -1, 0, 0) == BLOCK_TYPE_EMPTY);
  fail_after = -1;
  assert(world_set_block(&world, -1, 0, 0, BLOCK_TYPE_GRASS));
  SectionMesh mesh = {0};
  assert(section_mesh_build(&world, -1, 0, 0, &mesh));
  SurfaceVertex *original = mesh.vertices;
  SectionMesh originalMesh = mesh;
  size_t originalCount = mesh.vertexCount;
  SurfaceVertex *saved = malloc(originalCount * sizeof(*saved));
  assert(saved);
  for (size_t i = 0; i < originalCount; i++) saved[i] = mesh.vertices[i];
  const Subchunk *section = world_section(&world, -1, 0, 0);
  fail_after = 0;
  assert(!section_mesh_build(&world, -1, 0, 0, &mesh));
  assert(mesh.vertices == original && mesh.vertexCount == originalCount && section->dirty);
  for (int type = 0; type < BLOCK_TYPE_COUNT; type++) {
    assert(mesh.firstVertex[type] == originalMesh.firstVertex[type]);
    assert(mesh.typeVertexCount[type] == originalMesh.typeVertexCount[type]);
  }
  for (size_t i = 0; i < originalCount; i++) {
    assert(mesh.vertices[i].x == saved[i].x);
    assert(mesh.vertices[i].y == saved[i].y);
    assert(mesh.vertices[i].z == saved[i].z);
  }
  fail_after = -1;
  free(saved);
  section_mesh_destroy(&mesh);
  /* Reject every allocation in each generator; ownership and content survive. */
  for (int generator = 0; generator < 3; generator++) {
    World reference = {0}, sentinel = {0};
    size_t before = allocation_calls;
    assert(generate_world(generator, &reference));
    size_t allocations = allocation_calls - before;
    assert(world_init(&sentinel, 32, (Vec3){0}, 1));
    assert(world_set_block(&sentinel, -1, 0, 0, BLOCK_TYPE_GRASS));
    /* Discover allocation sites from a successful run, rather than guessing a
       fixed count. A fallback may succeed; it must still produce the full result. */
    for (size_t i = 0; i < allocations; i++) {
      world_destroy(&world);
      assert(world_init(&world, 32, (Vec3){0}, 1));
      assert(world_set_block(&world, -1, 0, 0, BLOCK_TYPE_GRASS));
      Chunk *chunks = world.chunks;
      const Subchunk *originalSection = world_section(&world, -1, 0, 0);
      fail_after = (int)i;
      int success = generate_world(generator, &world);
      fail_after = -1;
      if (success) {
        assert_world_equal(&world, &reference);
      } else {
        /* Ownership is explicitly transactional; check data as well as identity. */
        assert(world.chunks == chunks);
        assert(world_section(&world, -1, 0, 0) == originalSection);
        assert_world_equal(&world, &sentinel);
        assert(world_set_block(&world, 0, 0, 0, BLOCK_TYPE_STONE));
        assert(world_get_block(&world, 0, 0, 0) == BLOCK_TYPE_STONE);
      }
    }
    assert(generate_world(generator, &world));
    assert_world_equal(&world, &reference);
    world_destroy(&reference);
    world_destroy(&sentinel);
  }
  world_destroy(&world);
  /* Chunk generation commits all its sections together, including under OOM. */
  World streamed = {0};
  assert(create_terrain_streamed(&streamed, 33, (Vec3){0}, 1, 1));
  size_t before = allocation_calls;
  assert(world_materialize_chunk(&streamed, 0, 0));
  size_t allocations = allocation_calls - before;
  for (size_t i = 0; i < allocations; i++) {
    world_destroy(&streamed);
    assert(create_terrain_streamed(&streamed, 33, (Vec3){0}, 1, 1));
    TerrainColumn column = terrain_sample_column(1, 0, 0);
    fail_after = (int)i;
    assert(!world_materialize_chunk(&streamed, 0, 0));
    fail_after = -1;
    assert(streamed.sectionCount == 0 && streamed.voxelCount == 0);
    assert(!streamed.hasMaterializedBounds);
    for (int sy = WORLD_MIN_SECTION_Y; sy < WORLD_MIN_SECTION_Y + WORLD_SECTION_COUNT; sy++)
      assert(world_section(&streamed, 0, sy, 0) == NULL);
    assert(world_get_block(&streamed, 0, column.height - 1, 0) == column.material);
    assert(world_materialize_chunk(&streamed, 0, 0));
    assert(world_get_block(&streamed, 0, column.height - 1, 0) == column.material);
  }
  /* Sparse out-of-envelope insertion must preserve existing sections on OOM. */
  for (int y = -161; y <= 416; y += 577) {
    size_t sections = streamed.sectionCount, voxels = streamed.voxelCount;
    fail_after = 0;
    assert(!world_set_block(&streamed, 0, y, 0, BLOCK_TYPE_SNOW));
    fail_after = -1;
    assert(streamed.sectionCount == sections && streamed.voxelCount == voxels);
    assert(world_get_block(&streamed, 0, y, 0) == BLOCK_TYPE_EMPTY);
    assert(world_set_block(&streamed, 0, y, 0, BLOCK_TYPE_SNOW));
    assert(world_get_block(&streamed, 0, y, 0) == BLOCK_TYPE_SNOW);
  }
  world_destroy(&streamed);
  puts("All allocation failure tests passed.");
  return 0;
}
