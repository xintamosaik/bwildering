#ifndef WORLD_STREAMING_H
#define WORLD_STREAMING_H

#include "game_logic.h"
#include "performance.h"

typedef enum { STREAM_FAILED = 0, STREAM_COMPLETE = 1, STREAM_DEFERRED = 2 } StreamResult;
typedef struct {
  int minX, maxX, minZ, maxZ;
  Vec3 eye; /* Priority origin, independent of player ownership. */
} StreamRegion;

/* At block size 1 and radius 300 the padded bound is 41*41 columns.
   Sections share this fixed queue; excess work stays dirty and is rescanned
   from a rotating cursor. Smaller blocks and arbitrary edits need no larger queue. */
enum { STREAM_TASK_CAPACITY = 41 * 41 };
typedef struct {
  int x, y, z, materialize;
  uint64_t revision, born;
  double priority;
} StreamTask;
typedef struct {
  double seconds; /* Soft deadline; zero disables the clock limit. */
  size_t operations, bytes; /* Check before each atomic operation. */
} StreamBudget;

typedef struct {
  StreamTask pending[STREAM_TASK_CAPACITY];
  size_t pendingCount, pendingWork, peakPending, scanCursor;
  uint64_t streamFrame;
} WorldStreaming;

/* Optional cache consumer. Calls borrow world data only until return. retire
   must delete departing resources without allocating. The coordinator then
   cancels identities before recycling CPU caches, even if prepare later fails.
   sync acknowledges dirty/revision only after successful transactional upload;
   failure preserves dirty state and valid old geometry. Zero means failure,
   STREAM_DEFERRED means metadata must be recollected before this identity runs.
   Queued work uses coordinates and revisions. No callback may retain borrowed
   section pointers; consumer-owned mesh indices must be invalidated before
   replacing its metadata. retire/prepare/sync are required; drain is required
   for synchronous updates and nonprocedural worlds. */
typedef struct {
  void *context;
  void (*retire)(void *context, World *world, StreamRegion region);
  int (*prepare)(void *context, const World *world, StreamRegion region, int bounded);
  StreamResult (*sync)(void *context, const World *world, StreamTask task, size_t *bytes);
  int (*drain)(void *context, const World *world);
} StreamConsumer;

/* Inclusive padded square, clipped to the world. Caller supplies policy radius. */
int world_stream_region(const World *world, Vec3 eye, float radius, StreamRegion *region);
int world_stream_storage_limits(World *world, float maximumRadius);
/* Zero-initialize one coordinator per world, and reset it when replacing that
   world. Region bounds must come from world_stream_region or be clipped to the
   same world; min > max requests an empty cache.
   NULL consumer exercises CPU residency only, leaving mesh dirty state intact.
   NULL budget drains synchronously. Otherwise all materialize/mesh/upload work
   shares this one allowance, checked before each indivisible operation. */
StreamResult world_stream_update(WorldStreaming *stream, World *world, StreamRegion region,
                                const StreamBudget *budget, const StreamConsumer *consumer,
                                Performance *performance);
const Chunk *stream_chunk(const World *world, int x, int z);
int stream_identity_compare(const void *a, const void *b);

#endif
