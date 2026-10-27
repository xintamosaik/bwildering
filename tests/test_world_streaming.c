#include "../world_generation.h"
#include "../world_streaming.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static const uint32_t SEED = UINT32_C(0x4B1D2A37);

static size_t resident_count(const World *const world) {
  size_t count = 0;
  for (size_t i = 0; i < world->chunkCount; i++)
    count += world->chunks[i].terrainReady || world->chunks[i].sections;
  return count;
}

static void test_cpu_region_and_retry(void) {
  World world = {0};
  static WorldStreaming stream;
  assert(create_terrain_streamed(&world, 256, (Vec3){0}, 1.0f, SEED));
  assert(world_stream_storage_limits(&world, 25.0f));
  StreamRegion region;
  assert(world_stream_region(&world, (Vec3){-0.5f, 410.0f, -0.5f}, 25.0f, &region));
  assert(region.minX == -3 && region.maxX == 2 && region.minZ == -3 && region.maxZ == 2);
  assert(!world_stream_region(&world, (Vec3){NAN, 0.0f, 0.0f}, 25.0f, &region));
  const StreamBudget paused = {.operations = 0, .bytes = SIZE_MAX};
  const StreamBudget one = {.operations = 1, .bytes = SIZE_MAX};
  Performance perf = {.enabled = 1};
  assert(world_stream_update(&stream, &world, region, &paused, NULL, &perf) == STREAM_DEFERRED);
  assert(!resident_count(&world));
  assert(stream.pending[0].materialize && stream.pending[0].x == -1 && stream.pending[0].z == -1);
  assert(world_stream_update(&stream, &world, region, &one, NULL, &perf) == STREAM_DEFERRED);
  assert(resident_count(&world) == 1 && perf.frame.operations == 1);
  const StreamBudget noBytes = {.operations = 128, .bytes = 0};
  assert(world_stream_update(&stream, &world, region, &noBytes, NULL, &perf) == STREAM_DEFERRED);
  assert(resident_count(&world) == 1);
  assert(world_stream_update(&stream, &world, region, NULL, NULL, &perf) == STREAM_COMPLETE);
  assert(resident_count(&world) == 36);
  assert(world_set_block(&world, -1, 410, -1, BLOCK_TYPE_SNOW));
  assert(world_stream_region(&world, (Vec3){10000.0f, 410.0f, 0.0f}, 25.0f, &region));
  assert(world_stream_update(&stream, &world, region, &paused, NULL, &perf) == STREAM_COMPLETE);
  assert(!resident_count(&world) && !stream.pendingCount);
  assert(world_get_block(&world, -1, 410, -1) == BLOCK_TYPE_SNOW);
  assert(world_stream_region(&world, (Vec3){-0.5f, 410.0f, -0.5f}, 25.0f, &region));
  assert(world_stream_update(&stream, &world, region, NULL, NULL, &perf) == STREAM_COMPLETE);
  assert(world_get_block(&world, -1, 410, -1) == BLOCK_TYPE_SNOW);
  /* CPU-only operation leaves mesh acknowledgements to a future consumer. */
  assert(world_section(&world, -1, voxel_section_coord(410), -1)->dirty);
  world_destroy(&world);

  world = (World){0};
  stream = (WorldStreaming){0};
  assert(create_terrain_streamed(&world, 64, (Vec3){0}, 1.0f, SEED));
  assert(world_storage_limits(&world, 0, 0));
  assert(world_stream_region(&world, (Vec3){0}, 25.0f, &region));
  assert(world_stream_update(&stream, &world, region, &one, NULL, NULL) == STREAM_FAILED);
  assert(!resident_count(&world) && stream.pendingCount);
  assert(world_stream_storage_limits(&world, 25.0f));
  assert(world_stream_update(&stream, &world, region, NULL, NULL, NULL) == STREAM_COMPLETE);
  assert(resident_count(&world) == 16);
  world_destroy(&world);
}

/* A cache consumer observes retirement before CPU recycling and cancellation
   before prepare, including when prepare cannot allocate replacement metadata. */
typedef struct {
  WorldStreaming *stream;
  int departing, retired, failPrepare;
  size_t syncs;
} Audit;

static void retire(void *const context, World *const world, const StreamRegion region) {
  Audit *const audit = context;
  (void)region;
  if (audit->departing) {
    assert(resident_count(world));
    assert(audit->stream->pendingCount);
  }
  audit->retired = 1;
}

static int prepare(void *const context, const World *const world,
                   const StreamRegion region, const int bounded) {
  Audit *const audit = context;
  (void)region;
  assert(bounded && audit->retired);
  if (audit->departing) assert(!resident_count(world) && !audit->stream->pendingCount);
  return !audit->failPrepare;
}

static StreamResult sync_mesh(void *const context, const World *const world,
                              const StreamTask task, size_t *const bytes) {
  Audit *const audit = context;
  const Subchunk *const section = world_section(world, task.x, task.y, task.z);
  assert(section && section->dirty && section->revision == task.revision);
  audit->syncs++;
  *bytes += 64;
  /* Deliberately retain dirty state to model deferred acknowledgement. */
  return STREAM_COMPLETE;
}

static void test_shared_allowance_and_retirement(void) {
  World world = {0};
  static WorldStreaming stream;
  assert(create_terrain_streamed(&world, 128, (Vec3){0}, 1.0f, SEED));
  StreamRegion region;
  assert(world_stream_region(&world, (Vec3){-0.5f, 410.0f, -0.5f}, 25.0f, &region));
  Audit audit = {.stream = &stream};
  const StreamConsumer consumer = {&audit, retire, prepare, sync_mesh, NULL};
  Performance perf = {.enabled = 1};
  const StreamBudget one = {.operations = 1, .bytes = SIZE_MAX};
  assert(world_stream_update(&stream, &world, region, &one, &consumer, &perf) == STREAM_DEFERRED);
  assert(perf.frame.operations == 1 && perf.frame.materializedChunks == 1 && !audit.syncs);
  /* Edits win over missing columns; a mesh and its upload use the same allowance. */
  assert(world_set_block(&world, -1, 410, -1, BLOCK_TYPE_STONE));
  perf.frame = (FrameWork){0};
  assert(world_stream_update(&stream, &world, region, &one, &consumer, &perf) == STREAM_DEFERRED);
  assert(perf.frame.operations == 1 && !perf.frame.materializedChunks && audit.syncs == 1);
  const StreamBudget bytes = {.operations = 128, .bytes = 1};
  audit.syncs = 0;
  assert(world_stream_update(&stream, &world, region, &bytes, &consumer, &perf) == STREAM_DEFERRED);
  assert(audit.syncs == 1);
  audit.departing = audit.failPrepare = 1;
  assert(world_stream_region(&world, (Vec3){10000.0f, 410.0f, 0.0f}, 25.0f, &region));
  assert(world_stream_update(&stream, &world, region, &one, &consumer, &perf) == STREAM_FAILED);
  assert(!resident_count(&world) && !stream.pendingCount);
  assert(world_get_block(&world, -1, 410, -1) == BLOCK_TYPE_STONE);
  world_destroy(&world);
}

int main(void) {
  test_cpu_region_and_retry();
  test_shared_allowance_and_retirement();
  puts("Headless world streaming tests passed.");
  return 0;
}
