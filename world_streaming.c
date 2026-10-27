#define _POSIX_C_SOURCE 200809L
#include "world_streaming.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double stream_clock(void) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
}
static double stream_perf_clock(const Performance *const performance) {
  return performance && performance->enabled ? stream_clock() : 0.0;
}

/* Gameplay supports radius <= maximumRadius. Include padded residency,
   one node per authoritative edit, and one full transactional replacement column.
   This configures limits only; pages are committed on demand. */
int world_stream_storage_limits(World *const world, const float maximumRadius) {
  const double span = (double)world->blockSize * SECTION_SIZE;
  const size_t columns = (size_t)fmin(ceil(2.0 * (double)maximumRadius / span) + 3.0,
                                       world->chunksPerSide);
  const size_t slots = columns * columns * WORLD_SECTION_COUNT +
      WORLD_EDIT_CAPACITY + WORLD_SECTION_COUNT;
  return world_storage_limits(world, slots, slots);
}

const Chunk *stream_chunk(const World *const world, const int x, const int z) {
  return &world->chunks[(size_t)(x - world->minChunkX) * (size_t)world->chunksPerSide +
                        (size_t)(z - world->minChunkZ)];
}

int stream_identity_compare(const void *const a, const void *const b) {
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

static void stream_admit(WorldStreaming *const r, const StreamTask task, const size_t retained,
                         const size_t ordinal, const int pass, size_t *const nextCursor) {
  if ((ordinal < r->scanCursor) != (pass != 0) || r->pendingCount == STREAM_TASK_CAPACITY) return;
  if (bsearch(&task, r->pending, retained, sizeof(task), stream_identity_compare)) return;
  r->pending[r->pendingCount++] = task;
  *nextCursor = ordinal + 1;
}

int world_stream_region(const World *const world, const Vec3 eye, const float radius,
                        StreamRegion *const region) {
  if (!isfinite(eye.x) || !isfinite(eye.y) || !isfinite(eye.z) ||
      !isfinite(radius) || radius <= 0.0f) return 0;
  const float span = world->blockSize * SECTION_SIZE;
  const int maxChunkX = world->minChunkX + world->chunksPerSide - 1;
  const int maxChunkZ = world->minChunkZ + world->chunksPerSide - 1;
  const float loX = floorf((eye.x - world->origin.x - radius) / span) - 1;
  const float hiX = floorf((eye.x - world->origin.x + radius) / span) + 1;
  const float loZ = floorf((eye.z - world->origin.z - radius) / span) - 1;
  const float hiZ = floorf((eye.z - world->origin.z + radius) / span) + 1;
  const int minX = (int)fminf((float)maxChunkX + 1, fmaxf((float)world->minChunkX, loX));
  const int maxX = (int)fmaxf((float)world->minChunkX - 1, fminf((float)maxChunkX, hiX));
  const int minZ = (int)fminf((float)maxChunkZ + 1, fmaxf((float)world->minChunkZ, loZ));
  const int maxZ = (int)fmaxf((float)world->minChunkZ - 1, fminf((float)maxChunkZ, hiZ));

  *region = (StreamRegion){minX, maxX, minZ, maxZ, eye};
  return 1;
}

StreamResult world_stream_update(WorldStreaming *const stream, World *const world,
                                const StreamRegion region, const StreamBudget *const budget,
                                const StreamConsumer *const consumer, Performance *const performance) {
  if (!world->proceduralTerrain)
    return !consumer || consumer->drain(consumer->context, world) ? STREAM_COMPLETE : STREAM_FAILED;
  const Vec3 eye = region.eye;
  const float span = world->blockSize * SECTION_SIZE;
  const int minX = region.minX, maxX = region.maxX, minZ = region.minZ, maxZ = region.maxZ;
  const double retirementStart = stream_perf_clock(performance);
  if (consumer) consumer->retire(consumer->context, world, region);
  size_t retainedTasks = 0;
  for (size_t i = 0; i < stream->pendingCount; i++) {
    const StreamTask task = stream->pending[i];
    if (task.x >= minX && task.x <= maxX && task.z >= minZ && task.z <= maxZ)
      stream->pending[retainedTasks++] = task;
  }
  stream->pendingCount = retainedTasks;
  const size_t evicted = world_release_chunks_outside(world, minX, maxX, minZ, maxZ);
  if (performance && performance->enabled) {
    performance->frame.evictedChunks += evicted;
    performance->frame.retirement += stream_perf_clock(performance) - retirementStart;
  }
  if (budget) {
    const double collectionStart = stream_perf_clock(performance);
    /* Collection owns no section pointers. Invalidate departed/clean tasks and
       refresh revisions before executing; edits retain their original age. */
    stream->streamFrame++;
    size_t n = 0;
    for (size_t i = 0; i < stream->pendingCount; i++) {
      StreamTask task = stream->pending[i];
      if (task.x < minX || task.x > maxX || task.z < minZ || task.z > maxZ) continue;
      const Chunk *const chunk = stream_chunk(world, task.x, task.z);
      const Subchunk *const section = task.materialize ? NULL : world_section(world, task.x, task.y, task.z);
      if (task.materialize ? chunk->terrainReady : !consumer || !section || !section->dirty) continue;
      if (section) task.revision = section->revision;
      stream->pending[n++] = task;
    }
    stream->pendingCount = n;
    qsort(stream->pending, n, sizeof(*stream->pending), stream_identity_compare);
    const size_t retained = n;
    size_t ordinal = 0, nextCursor = stream->scanCursor;
    stream->pendingWork = 0;
    /* Two passes wrap the admission cursor without borrowing mutable records. */
    for (int pass = 0; pass < 2; pass++) {
      ordinal = 0;
      for (int x = minX; x <= maxX; x++) for (int z = minZ; z <= maxZ; z++) {
        const Chunk *const chunk = stream_chunk(world, x, z);
        if (!chunk->terrainReady) {
          const StreamTask task = {.x=x, .z=z, .materialize=1, .born=stream->streamFrame};
          stream_admit(stream, task, retained, ordinal++, pass, &nextCursor);
        }
        for (const Subchunk *section = consumer ? chunk->sections : NULL; section; section = section->next) {
          if (!section->dirty) continue;
          const StreamTask task = {.x=x, .y=section->sectionY, .z=z,
              .revision=section->revision, .born=stream->streamFrame};
          stream_admit(stream, task, retained, ordinal++, pass, &nextCursor);
        }
      }
    }
    stream->pendingWork = ordinal;
    stream->scanCursor = ordinal ? nextCursor % ordinal : 0;
    if (stream->pendingCount > stream->peakPending) stream->peakPending = stream->pendingCount;
    const double eyeX = (double)(eye.x - world->origin.x) / (double)span;
    const double eyeZ = (double)(eye.z - world->origin.z) / (double)span;
    for (size_t i = 0; i < stream->pendingCount; i++) {
      StreamTask *const task = &stream->pending[i];
      const Chunk *const chunk = stream_chunk(world, task->x, task->z);
      /* Aging eventually dominates both distance and the edit preference. */
      task->priority = fabs((double)task->x + 0.5 - eyeX) + fabs((double)task->z + 0.5 - eyeZ)
          + (task->materialize ? 0.0 : fabs((double)task->y + 0.5 -
              (double)(eye.y - world->origin.y) / (double)span) * 0.25)
          - (double)(stream->streamFrame - task->born) * 0.5 - (chunk->edited ? 64.0 : 0.0);
    }
    qsort(stream->pending, stream->pendingCount, sizeof(*stream->pending), stream_priority_compare);
    if (performance && performance->enabled)
      performance->frame.metadata += stream_perf_clock(performance) - collectionStart;
  } else {
    stream->pendingCount = stream->pendingWork = 0;
    for (int cx = minX; cx <= maxX; cx++) for (int cz = minZ; cz <= maxZ; cz++) {
      const int missing = !stream_chunk(world, cx, cz)->terrainReady;
      const double start = stream_perf_clock(performance);
      const int ready = world_materialize_chunk(world, cx, cz);
      if (performance && performance->enabled) {
        FrameWork *const f = &performance->frame;
        const double elapsed = stream_perf_clock(performance) - start;
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

  if (consumer && !consumer->prepare(consumer->context, world, region, budget != NULL))
    return STREAM_FAILED;
  if (!budget)
    return !consumer || consumer->drain(consumer->context, world) ? STREAM_COMPLETE : STREAM_FAILED;
  const double workStart = stream_clock();
  size_t done = 0, bytes = 0;
  StreamResult result = STREAM_DEFERRED;
  while (done < stream->pendingCount && done < budget->operations && bytes < budget->bytes) {
    if (budget->seconds > 0.0 && stream_clock() - workStart >= budget->seconds) break;
    const StreamTask task = stream->pending[done];
    const double start = stream_clock();
    int success;
    if (task.materialize) {
      success = world_materialize_chunk(world, task.x, task.z);
      if (performance && performance->enabled) {
        performance->frame.materialize += stream_clock() - start;
        performance->frame.materializedChunks += success != 0;
      }
    } else {
      const Subchunk *const section = world_section(world, task.x, task.y, task.z);
      if (!section || !section->dirty || section->revision != task.revision) break;
      const StreamResult synced = consumer->sync(consumer->context, world, task, &bytes);
      if (synced == STREAM_DEFERRED) break;
      success = synced == STREAM_COMPLETE;
    }
    if (performance && performance->enabled) {
      FrameWork *const f = &performance->frame;
      f->operations++;
      const double elapsed = stream_clock() - start;
      if (elapsed > f->atomicMax) f->atomicMax = elapsed;
    }
    if (!success) { result = STREAM_FAILED; break; }
    done++;
  }
  memmove(stream->pending, stream->pending + done,
          (stream->pendingCount - done) * sizeof(*stream->pending));
  stream->pendingCount -= done;
  /* A materialization creates section work discovered on the next collection.
     Only an empty collection can certify stationary convergence. */
  return result == STREAM_FAILED ? result : stream->pendingWork ? STREAM_DEFERRED : STREAM_COMPLETE;
}

