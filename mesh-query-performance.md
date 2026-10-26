# Mesh neighbor query measurements

Issue [005](issues/005-mesh-neighbor-query-performance.md), 26 October 2026.
Build-local reuse removes repeated lookup and sampling work. Section storage
remains a sorted sparse list, including edits outside the generated Y range.

## Reproduce

```sh
make mesh_query_benchmark mesh_query_profile
./mesh_query_benchmark 500
./mesh_query_profile 50
```

These headless runners require only the C compiler and libm. The optional integer
argument is the number of repeated builds, after one warmup. Assertions must be
enabled. Each fixture first compares exact vertex bytes, vertex counts and
material ranges with the frozen pre-005 mesher in
[tests/mesh_query_baseline.inc](tests/mesh_query_baseline.inc). Baseline and current
share terrain generation, allocation and world lookup helpers. The benchmark
includes voxel_world.c directly and links game_logic.c; it needs no OpenGL.

All fixtures use streamed terrain, seed 1, a 96-block square, origin zero, unit
blocks, and target chunk (-1,-1). Surface Y is the section containing the top
block at (-8,-8). World setup and materialization are outside the timer.

| Fixture | Resident data and edits |
| --- | --- |
| Resident surface | 3x3 materialized chunk neighborhood |
| Synthetic neighbors | Only target chunk materialized |
| Synthetic target | No chunks materialized; includes temporary target generation |
| Edited deep border | Target materialized, section Y=-1; remove (0,-8,-8) in an otherwise synthetic neighbor |
| Sparse high edit | One snow block at (-8,16008,-8), section Y=1000; no materialization |

Timing includes replacement mesh allocation and destruction of the previous
mesh. The last mesh is destroyed outside the timer. This is a warm repeated CPU
microbenchmark, not a streaming or frame-time measurement; allocator, CPU clock
and concurrent host load affect results. Each scenario measures baseline first.
No timing threshold is enforced.

## Uninstrumented build times

Representative run: Intel Core i5-6500 @ 3.20 GHz, Linux x86_64,
GCC 16.2.1, `-Wall -Wextra -O2 -std=c99`, 500 builds per row.

| Fixture | Baseline us/build | Current us/build | Speedup | Vertices |
| --- | ---: | ---: | ---: | ---: |
| Resident surface | 865.643 | 55.799 | 15.5x | 2004 |
| Synthetic neighbors | 1201.520 | 72.525 | 16.6x | 2004 |
| Synthetic target | 8837.562 | 191.430 | 46.2x | 2004 |
| Edited deep border | 1212.360 | 67.404 | 18.0x | 6 |
| Sparse high edit | 7.381 | 7.704 | 0.96x | 36 |

Sparse single-block edits show a small absolute regression (0.323 us in this
run). Both versions scan the section twice; the new version also initializes
build-local caches and resolves six section pointers. Accept this tradeoff for
the substantial terrain-build savings; revisit if isolated sparse edits become
a measured frame cost.

## Query profile

A separate instrumented executable records monotonic clock intervals and counts.
The normal game and uninstrumented benchmark compile out every hook.

| Fixture | Section lookups before → after | List links followed before → after | Column samples before → after |
| --- | ---: | ---: | ---: |
| Resident surface | 43165 → 7 | 431258 → 70 | 0 → 0 |
| Synthetic neighbors | 43165 → 7 | 413538 → 30 | 1772 → 64 |
| Synthetic target | 43165 → 7 | 0 → 0 | 43420 → 576 |
| Edited deep border | 49153 → 7 | 329735 → 21 | 1536 → 48 |
| Sparse high edit | 13 → 7 | 0 → 1 | 0 → 0 |

Instrumented times from 50 builds, microseconds per build:

| Fixture | Neighbor work before → after | Section lookup before → after | Column sampling before → after |
| --- | ---: | ---: | ---: |
| Resident surface | 3889.797 → 61.393 | 1440.076 → 0.573 | 0 → 0 |
| Synthetic neighbors | 4311.220 → 79.855 | 1421.680 → 0.241 | 370.617 → 13.047 |
| Synthetic target | 14071.494 → 141.259 | 1048.469 → 0.169 | 9043.226 → 118.674 |
| Edited deep border | 4626.733 → 82.833 | 1447.671 → 0.203 | 322.750 → 9.868 |
| Sparse high edit | 0.974 → 5.769 | 0.320 → 0.172 | 0 → 0 |

These are inclusive, overlapping intervals, **not additive costs**. Baseline
neighbor time sums each world_get_block call; current neighbor time covers the
whole visibility/count pass, including loop work and section resolution. Lookup
time covers world_section, including chunk location, traversal and timer
reads. Link counts count next-pointer advances, not final node comparisons.
Sampling covers the noise/terrain-column function, including the temporary
target's 256 samples in the synthetic-target fixture. Those target-generation
samples are outside the neighbor interval in both versions.

Per-call clocks strongly inflate baseline lookup/query timings; do not subtract
these from the uninstrumented totals or infer exact percentages from them. The
counts identify repeated work, the profile distinguishes sampling from lookup,
and the separate uninstrumented before/after run establishes the actual gain.
Synthetic-target sampling is the largest measured category; resident terrain
instead repeats long section traversals despite needing no noise sampling.

## Implemented change and deferred options

The first pass reads interior neighbors directly from the target section,
resolves the six adjacent sections once, and lazily samples each required
boundary X/Z column once. It records six visibility bits per voxel and material
counts. The emission pass reuses those bits, retaining vertex order, winding and
contiguous material ranges. Synthetic target generation is unchanged.

The cache lives on the build stack (about 14 KiB including the visibility mask),
adds no heap allocation, and expires after each call. Reads check world bounds;
allocated sections, including empty edit markers, override synthetic terrain.
Missing sections in a terrain-ready chunk remain air. Procedural fallback keeps
the existing generation-height limits. No world ownership, dirty flags,
transactional replacement rules or sparse-edit limits change.

Direct indexing for the normal 32 terrain sections is deferred: meshing now
makes only seven section lookups and measured lookup time is below one
microsecond in the terrain fixtures. Adding persistent indexing and sparse
fallback complexity has no demonstrated benefit here. Issue 006 can pursue
uniform-section storage for memory savings independently, while retaining this
build-local reuse. Persistent terrain-column caches and full neighboring
section materialization are also deferred: lazy boundary reuse suffices without
cross-build invalidation or extra residency.

## Correctness checks

- The benchmark checks exact geometry and material ranges against the old mesher
  for all five fixtures before measuring.
- `make test` passes logic, allocation-failure and headless menu tests. Existing
  independent face-oracle tests cover all six section boundaries, multiple
  materials, resident/synthetic neighbors and edits before materialization.
- `test_mesh_neighbor_cache_lifetime` additionally compares faces with world
  reads across edits, restoration, neighbor materialization and eviction in a
  partially clipped negative chunk, at terrain boundaries and both edit limits.
- `ASAN_OPTIONS=detect_leaks=0 make test-sanitize` passes ASan/UBSan. LeakSanitizer
  cannot run under this environment's tracing; leak detection is excluded.
- `make all` builds without warnings. `make test-renderer` builds, but execution
  cannot open this environment's X11 display `:0`, so GL validation is pending.
