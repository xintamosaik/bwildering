# EXPERIMENTAL: distant terrain at native resolution

Status: implemented on `main`, ready for playtesting. The independent 1280-wide
internal-resolution experiment is saved on `experiment/internal-resolution-1280`.
This experiment uses native framebuffer resolution and the existing 300-unit
view distance, so comparisons do not mix render scaling with distant meshes.

## Try it

Normal launch enables distant terrain. Restart with
`env BW_DISTANT_TERRAIN=0 ./bwildering` for the full voxel-rendering baseline,
or `env BW_DISTANT_TERRAIN=1 ./bwildering` to enable it explicitly.
Hold camera position/direction, display resolution, and view distance fixed.
No render-resolution option from the other branch is active here.

## Current approach

- Full voxel geometry within a square neighborhood of roughly 96 units per
  axis. Boundaries align to chunks and move when the camera crosses a chunk.
  The square's corners extend farther than 96 units; this is not a circular cut.
- Beyond that neighborhood, an 8-voxel surface grid replaces unedited terrain
  columns. Shared sample points match between coarse cells. One-block seam
  segments bridge coarse edges to the actual neighboring voxel surface, so
  joins can close differences above or below the coarse interpolation.
- Surface height, grass/sand/stone selection, and seabed come from the same
  generator as the full world. A flat water surface covers submerged cells.
- One GPU mesh contains material ranges for the distant ring, reducing draw
  calls. Distant land omits block color/grid detail. Distant water retains its
  viewing-angle tint/transparency but omits animated ripple/specular lighting.
- A fixed-capacity cache reuses geometry until the chunk neighborhood, generation
  options/transform, or edited-column mask changes. Camera rotation needs no
  rebuild. Static scratch buffers avoid per-cell heap allocation.

The implementation lives in `distant_terrain.c/.h`; application/rendering select
the mode without changing world generation, collision, physics, or editing.

## Fallbacks and current limits

Edited columns and their face-adjacent neighbors always use full voxel meshes,
including modifications far above the surface and removals. The neighbor apron
preserves faces exposed by nearby edits. The actual edited-column mask invalidates coarse
geometry even when the total number of edits stays unchanged. Underwater views,
views below solid support, nonprocedural worlds, and distances at or below 96
use full geometry. Regions larger than the fixed 41×41-column capacity also fall
back; unit blocks at view distance 300 fit. Larger physical blocks are supported
when they fit; very small blocks can exceed this capacity.

Failed coarse uploads retain the old resources but deactivate replacement for
that frame; full geometry is available as the fallback. Successful replacements
transfer ownership and release the previous buffers. World shutdown releases
both caches.

This first version changes rendering only. Full voxel residency and meshes are
still generated out to the view distance, so it does not yet reduce streaming
work or world-memory use. It adds a small coarse GPU cache instead. Rebuilding
the ring is synchronous when its plan changes and is outside the existing
streaming work budget; moving-boundary stalls remain something to measure.
There is no geometric morphing, continuous transition in shading, or larger
horizon yet. Expect some popping, simpler coast/summit contours, and material
changes at coarse boundaries. The heightfield does not represent caves or
overhangs; edited columns use the full renderer to preserve their geometry.

## Evidence and reproducible comparison

`make distant_terrain_capture` builds a real-GL comparison runner. It warms each
mode, then measures 180 completed frames with UI and swaps at identical camera
positions. `./distant_terrain_capture` writes world-only captures to
`/tmp/distant-terrain-{other-island,mountain}-{full,coarse}.ppm`.

Initial local AMD Radeon HD 7800 measurements at native 1920×1200, distance 300:

| View | Full FPS | Distant FPS | Full → distant draw calls | Full → distant triangles |
| --- | --- | --- | --- | --- |
| Other island | 414 | 589 | 949 → 136 | 462,208 → 92,022 |
| Inland mountain | 867 | 1,725 | 876 → 78 | 407,726 → 65,112 |

These are local comparison measurements, not predictions for fullscreen 5K.
The ring used about 1 MiB of additional GPU vertex data in those views.

`test_renderer` checks reduced submission, depth parity for near pixels, cache
reuse, edit-mask changes, transactional upload failure/retry, underwater and
underground fallback, scaled world replacement, capacity fallback, and resource
cleanup. Existing rendering/application and pause-menu checks remain applicable.
Clang-Tidy narrowing-conversion checks cover changed C sources.
