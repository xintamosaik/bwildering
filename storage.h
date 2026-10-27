#ifndef STORAGE_H
#define STORAGE_H
#include <stddef.h>

typedef enum {
  STORAGE_WORLD, STORAGE_NODE, STORAGE_PAYLOAD, STORAGE_MESH,
  STORAGE_RECORD, STORAGE_VISIBLE, STORAGE_DRAW, STORAGE_CATEGORY_COUNT
} StorageCategory;
typedef struct {
  size_t attempts, requested, live, peakLive, capacity, peakCapacity;
  size_t systemAllocations, reuse, failures;
} StorageStats;
/* Single-threaded opt-in diagnostic state. Enable before creating owners and
   disable after destruction; no clocks or allocator/RSS estimates are included. */
void storage_accounting(int enabled);
const StorageStats *storage_stats(StorageCategory category);
/* Test-only reservation failure hook. Also sees reuse, independent of malloc. */
extern int (*storage_reject)(StorageCategory category);
int storage_reserve(StorageCategory category, size_t bytes, int reused);
void storage_exhausted(StorageCategory category);
void storage_release(StorageCategory category, size_t bytes);
void *storage_backing_alloc(StorageCategory category, size_t bytes);
void storage_backing_free(StorageCategory category, void *pointer, size_t bytes);
void *storage_alloc(StorageCategory category, size_t bytes);
void storage_free(StorageCategory category, void *pointer, size_t bytes);
#endif
