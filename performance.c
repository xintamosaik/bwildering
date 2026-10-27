#include "performance.h"

#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void perf_configure(Performance *const p) {
  p->enabled = getenv("BW_PERF") && strcmp(getenv("BW_PERF"), "0");
  p->overlay = getenv("BW_DEBUG") && strcmp(getenv("BW_DEBUG"), "0");
  if (p->overlay) p->enabled = 1;
}

double perf_clock(const Performance *const p) {
  return p && p->enabled ? glfwGetTime() : 0;
}

static int perf_compare(const void *const a, const void *const b) {
  const double x = *(const double *)a, y = *(const double *)b;
  return (x > y) - (x < y);
}

void perf_record(Performance *const p) {
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

void perf_finish_window(Performance *const p) {
  if (!p->count) return;
  double sorted[PERF_SAMPLES];
  memcpy(sorted, p->samples, p->count * sizeof(*sorted));
  qsort(sorted, p->count, sizeof(*sorted), perf_compare);
  p->average = p->total.raw / (double)p->count;
  p->p95 = sorted[(95 * p->count + 99) / 100 - 1];
  p->maximum = sorted[p->count - 1];
}

void performance_reset(Performance *const p) {
  p->count = 0;
  p->total = (FrameWork){0};
  p->frame = (FrameWork){0};
}

void perf_report(Performance *const p, const PerformanceSnapshot *const s, const char *const phase) {
  if (!p->enabled) return;
  perf_finish_window(p);
  const double n = p->count ? (double)p->count : 1;
  fprintf(stdout, "PERF phase=%s samples=%zu frame_ms=%.3f/%.3f/%.3f "
      "update_ms=%.3f sync_ms=%.3f materialize_ms=%.3f mesh_ms=%.3f upload_ms=%.3f "
      "render_ms=%.3f swap_ms=%.3f visibility_ms=%.3f distance_tests_avg=%.1f "
      "frustum_tests_avg=%.1f material_changes_avg=%.1f visibility_fallbacks=%zu "
      "rebuilds=%zu upload_calls=%zu upload_bytes=%zu evicted_chunks=%zu retired_meshes=%zu retired_bytes=%zu "
      "draws_avg=%.1f triangles_avg=%.1f resident_chunks=%zu sections=%zu edited=%zu dirty=%zu "
      "neighborhood_slots=%zu world_metadata_bytes=%zu renderer_metadata_bytes=%zu "
      "resident_voxel_bytes=%zu visible_index_bytes=%zu draw_index_bytes=%zu peak_cpu_mesh_bytes=%zu "
      "gpu_world_requested_bytes=%zu world_metadata_ms=%.3f renderer_metadata_ms=%.3f startup_ms=%.3f "
      "seed=%u side=%d distance=%.0f resolution=%dx%d swap_interval=0 overlay=%d paused=%d\n",
      phase, p->count, p->average*1000, p->p95*1000, p->maximum*1000,
      p->total.update*1000/n, p->total.sync*1000/n, p->total.materialize*1000/n,
      p->total.mesh*1000/n, p->total.upload*1000/n, p->total.render*1000/n, p->total.swap*1000/n,
      p->total.visibility*1000/n, (double)p->total.distanceTests/n, (double)p->total.frustumTests/n,
      (double)p->total.materialChanges/n, p->total.visibilityFallbacks,
      p->total.rebuilds, p->total.uploadCalls, p->total.uploadBytes,
      p->total.evictedChunks, p->total.retiredMeshes, p->total.retiredBytes,
      (double)p->total.draws/n, (double)p->total.triangles/n, s->residentChunks, s->sections,
      s->editedChunks, s->dirtySections, s->neighborhoodSlots,
      s->worldMetadataBytes,
      s->rendererMetadataBytes,
      s->residentVoxelBytes,
      s->visibleIndexBytes,
      s->drawIndexBytes, p->total.peakMeshBytes,
      s->gpuBytes, s->worldMetadataSeconds*1000, s->rendererMetadataSeconds*1000,
      s->startupSeconds*1000, s->seed, s->side,
      s->viewDistance, s->framebufferWidth, s->framebufferHeight,
      p->overlay, s->paused);
  fflush(stdout);
}

