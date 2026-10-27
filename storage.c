#include "storage.h"
#include <stdlib.h>
#include <string.h>
static int accounting;
static StorageStats stats[STORAGE_CATEGORY_COUNT];
int (*storage_reject)(StorageCategory category);
void storage_accounting(const int enabled) {
  accounting = enabled;
  if (enabled) memset(stats, 0, sizeof(stats));
}
const StorageStats *storage_stats(const StorageCategory category) { return &stats[category]; }
int storage_reserve(const StorageCategory category, const size_t bytes, const int reused) {
  const int rejected = storage_reject && storage_reject(category);
  if (accounting) {
    StorageStats *const s = &stats[category];
    s->attempts++;
    s->requested += bytes;
    s->failures += rejected != 0;
    if (!rejected) {
      s->reuse += reused != 0;
      s->live += bytes;
      if (s->live > s->peakLive) s->peakLive = s->live;
    }
  }
  return !rejected;
}
void storage_exhausted(const StorageCategory category) {
  if (accounting) stats[category].failures++;
}
void storage_release(const StorageCategory category, const size_t bytes) {
  if (accounting) stats[category].live -= bytes;
}
void *storage_backing_alloc(const StorageCategory category, const size_t bytes) {
  void *const p = malloc(bytes);
  if (accounting) {
    StorageStats *const s = &stats[category];
    s->systemAllocations++;
    if (p) {
      s->capacity += bytes;
      if (s->capacity > s->peakCapacity) s->peakCapacity = s->capacity;
    } else s->failures++;
  }
  return p;
}
void storage_backing_free(const StorageCategory category, void *const pointer, const size_t bytes) {
  if (!pointer) return;
  if (accounting) stats[category].capacity -= bytes;
  free(pointer);
}
void *storage_alloc(const StorageCategory category, const size_t bytes) {
  if (!storage_reserve(category, bytes, 0)) return NULL;
  void *const p = storage_backing_alloc(category, bytes);
  if (!p) storage_release(category, bytes);
  return p;
}
void storage_free(const StorageCategory category, void *const pointer, const size_t bytes) {
  if (!pointer) return;
  storage_release(category, bytes);
  storage_backing_free(category, pointer, bytes);
}
