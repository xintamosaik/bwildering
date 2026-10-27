#ifndef PERFORMANCE_H
#define PERFORMANCE_H

#include <stddef.h>

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

typedef struct Application Application;

void perf_configure(Performance *p);
double perf_clock(const Performance *p);
void perf_record(Performance *p);
void perf_finish_window(Performance *p);
void performance_reset(Performance *p);
void performance_snapshot(Application *application);
void performance_report(Application *application, const char *phase);
#endif
