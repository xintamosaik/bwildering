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

/* Application and renderer metadata collected at reporting boundaries. */
typedef struct {
  size_t residentChunks, sections, editedChunks, dirtySections, neighborhoodSlots;
  size_t worldMetadataBytes, rendererMetadataBytes, residentVoxelBytes;
  size_t visibleIndexBytes, drawIndexBytes, gpuBytes;
  double worldMetadataSeconds, rendererMetadataSeconds, startupSeconds;
  unsigned int seed;
  int side;
  float viewDistance;
  int framebufferWidth, framebufferHeight, paused;
} PerformanceSnapshot;

void perf_configure(Performance *p);
double perf_clock(const Performance *p);
void perf_record(Performance *p);
void perf_finish_window(Performance *p);
void performance_reset(Performance *p);
void perf_report(Performance *p, const PerformanceSnapshot *snapshot, const char *phase);
#endif
