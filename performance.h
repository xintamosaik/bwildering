#ifndef PERFORMANCE_H
#define PERFORMANCE_H

/* Fixed, disjoint windows; seconds internally. No allocation on the frame path. */
enum { PERF_SAMPLES = 240 };
typedef struct {
  double raw, update, sync, materialize, mesh, upload, render, swap, visibility;
  size_t distanceTests, frustumTests, materialChanges, visibilityFallbacks;
  size_t rebuilds, uploadCalls, uploadBytes, draws, triangles, peakMeshBytes;
  size_t evictedChunks, retiredMeshes, retiredBytes;
} FrameWork;
typedef struct {
  FrameWork frame, total;
  double samples[PERF_SAMPLES];
  size_t count;
  double average, p95, maximum;
  int enabled, overlay;
} Performance;

static double perf_clock(const Performance *p) {
  return p && p->enabled ? glfwGetTime() : 0;
}

static int perf_compare(const void *a, const void *b) {
  double x = *(const double *)a, y = *(const double *)b;
  return (x > y) - (x < y);
}

static void perf_record(Performance *p) {
  if (!p->enabled || p->count == PERF_SAMPLES) return;
  p->samples[p->count++] = p->frame.raw;
#define PERF_SUM(field) p->total.field += p->frame.field
  PERF_SUM(raw); PERF_SUM(update); PERF_SUM(sync); PERF_SUM(materialize);
  PERF_SUM(mesh); PERF_SUM(upload); PERF_SUM(render); PERF_SUM(swap);
  PERF_SUM(rebuilds); PERF_SUM(uploadCalls); PERF_SUM(uploadBytes);
  PERF_SUM(evictedChunks); PERF_SUM(retiredMeshes); PERF_SUM(retiredBytes);
  PERF_SUM(draws); PERF_SUM(triangles); PERF_SUM(visibility);
  PERF_SUM(distanceTests); PERF_SUM(frustumTests);
  PERF_SUM(materialChanges); PERF_SUM(visibilityFallbacks);
#undef PERF_SUM
  if (p->frame.peakMeshBytes > p->total.peakMeshBytes)
    p->total.peakMeshBytes = p->frame.peakMeshBytes;
}

static void perf_finish_window(Performance *p) {
  if (!p->count) return;
  double sorted[PERF_SAMPLES];
  memcpy(sorted, p->samples, p->count * sizeof(*sorted));
  qsort(sorted, p->count, sizeof(*sorted), perf_compare);
  p->average = p->total.raw / p->count;
  p->p95 = sorted[(95 * p->count + 99) / 100 - 1];
  p->maximum = sorted[p->count - 1];
}
#endif
