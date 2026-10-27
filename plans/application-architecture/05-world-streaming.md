# Stage 5: separate CPU residency policy from GPU synchronization

Status: implemented; verification recorded below. Order: 5 of 5. Prerequisite: [stage 4](04-tick-command-timing.md).

[Architecture overview and shared verification](../application-architecture.md).

## Problem description

Renderer synchronization currently chooses CPU residency, materializes chunks
and coordinates their eviction as well as managing meshes and GPU resources.
This couples CPU cache policy to one player's visual distance. A headless server
would need CPU residency without OpenGL, while multiple clients would eventually
need visual caches independent of authoritative simulation interest.

## Implementation

1. Move desired-residency calculation and CPU materialization scheduling to a
   small world-streaming module. Application supplies the current local region
   of interest; the renderer consumes resulting world data and mesh work.
   Preserve the single-player radius and residency bounds for now.
2. Keep one coordinated work budget across materialization, meshing and upload;
   splitting modules must not grant each a fresh full-frame allowance. Preserve
   nearest/edit priority, aging, bounded admission and synchronous drain support.
3. Make retirement order explicit: retire departing GPU resources, cancel tasks
   referring to departing identities, then recycle CPU caches. Preserve residency
   progress when renderer metadata allocation fails. Track tasks by coordinates
   and revision, never borrowed section pointers or unstable array indices.
4. Keep mesh dirty/revision handling behind a documented cache-sync contract.
   Do not introduce replicated dirty flags or a world-change message log yet.
5. Continue using view distance to request the local visual cache, but express
   that as application policy. The streaming module must not require a renderer
   or GL context. Its region input can later be computed from server simulation
   interest independently of client visual distance.

## Acceptance criteria

- CPU region calculation, materialization and eviction can be exercised without a renderer or GL context.
- One coordinated allowance still bounds newly admitted materialization, mesh and upload work; module separation does not multiply the budget.
- Retirement and cancellation precede CPU recycling; no queued work retains borrowed section pointers or unstable indices.
- Existing exhaustion, stale-revision, failure/retry, edit persistence and convergence fixtures pass, including renderer metadata failure during residency progress.
- Final meshes/material ranges match synchronous references and valid old geometry remains drawable during retries.
- Comparable serial captures document residency, operation counts and any timing changes; unrelated scheduler tuning is deferred.
- Application supplies the current local region policy; multi-player interest and replication remain deferred.

## Validation and completion evidence

Apply the [shared verification requirements](../application-architecture.md#verification-for-implementation-stages), including builds, affected tests and
Clang-Tidy checks. Record commands, results, limitations and any intentional
behavior changes here when implementing this stage.

`world_streaming.c/.h` now owns region calculation, CPU cache limits,
materialization, eviction, the inline 1,681-task queue, rotating admission,
revision refresh, edit/distance/age priority and coordinated execution. Application
embeds `WorldStreaming` beside Game and Renderer and supplies the local visual
radius. A NULL consumer runs CPU residency without GLFW/OpenGL linkage; a NULL
budget preserves the synchronous drain. The normal allowance remains 2 ms,
128 atomic operations and 2 MiB of requested section upload bytes. Collection,
retirement and metadata remain outside that soft new-work allowance.

Renderer exposes a narrow `StreamConsumer`: retire departing GPU resources,
prepare current mesh metadata, synchronize one coordinate/revision identity, or
drain current meshes. Retirement precedes queue cancellation, which precedes
CPU recycling; metadata preparation follows those committed departures. Shutdown
uses the same GPU → queue → CPU order. Pending work never stores borrowed section
pointers or record indices. Renderer retains the existing transactional section
upload, dirty acknowledgement, old-geometry fallback and retry behavior. The
cache-sync contract is documented in `world_streaming.h`; no duplicate dirty
flags, change log or per-frame allocations were added.

Fixtures and all capture targets now use application policy with separately
owned coordinator state. CPU-only tests exercise clipped/empty regions, nearest
materialization, zero/one-operation and byte allowances, exhaustion/retry,
edited departure/revisit, dirty-state preservation and observed retirement /
cancellation / recycling order even when metadata preparation fails. A cache
consumer fixture verifies materialization and mesh/upload share one allowance.
Existing real-GL fixtures retain independent mesh/material-range and depth
references, queue exhaustion/aging, stale revision, metadata/upload failures,
old geometry during retries and eventual synchronous convergence.

Full LeakSanitizer verification exposed an existing dense-reference fixture
leak: `test_section_storage` attached individual malloc blocks to pooled section
nodes. Its independent dense oracle now borrows fixed static rows and detaches
them before pooled teardown. This changes fixture ownership only; its terrain
and mesh comparisons remain intact.

No intended terrain, residency radius, scheduling priority or gameplay changes.
The coordinator uses a monotonic POSIX clock instead of GLFW for its deadline
and CPU instrumentation. Region inputs reject nonfinite priority origins,
including Y, and invalid radii. Performance ownership totals include the moved
queue under world metadata and capture CPU totals.

Verification and serial capture results follow.

- `make all` and `make test`: passed, including the new headless streaming
  fixture and existing game, terrain/collision, allocation-failure, routing and
  pool suites. `test_world_streaming` links only libc/libm, verified with `ldd`.
- `make test-renderer`: passed on the real AMD Radeon HD 7800 Series, Mesa
  26.2.5, X11 `:0`, including independent vertex/material-range and depth parity,
  failure/retry, exhaustion, revision refresh, edited revisits and application
  lifecycle checks. The sandbox cannot open the display; verification ran
  outside it against the same local display, without a virtual/software substitute.
- `make test-sanitize`: all six suites passed with ASan, UBSan and leak detection
  enabled after the fixture ownership repair. The sandbox's ptrace restriction
  prevents LeakSanitizer inspection; the successful full run was outside it.
- `make streaming_capture visibility_capture terrain_capture performance_capture
  allocation_capture local_vertex_capture`: all affected captures built using
  the production translation units and explicit coordinator ownership.
- Every changed/new C file checked with `clang-tidy
  --checks=-*,bugprone-narrowing-conversions -- -Wall -Wextra -O2 -std=c99`,
  using `-DBW_RENDERER_TEST` for the renderer and applicable fixture targets:
  no introduced warnings. `tests/test_game_logic.c` retains 62 existing narrowing
  diagnostics, also verified against the original revision; none are in the
  changed dense-reference code. All other changed C files have no diagnostics.
- `git diff --check`: passed.

Serial capture evidence (27 October 2026): original revision
`060766e66999e0143accebbd4efc6525d3331084` was exported to a separate temporary
source tree and compiled with the same GCC 16.2.1 `-O2 -std=c99` flags. After
builds and tests completed, the original and updated captures ran one at a time
on the same real GL display, seed 1260202551, resolution 640×480, swap interval 0,
radii 25/100/300, seven scenarios per radius and 240 normal frames per scenario.

```
/path/to/original/streaming_capture --frames > before-frames.csv
./streaming_capture --frames > after-frames.csv
/path/to/original/streaming_capture --baseline > before-baseline.csv
./streaming_capture --baseline > after-baseline.csv
```

Raw evidence: [original scheduled](streaming-measurements/before-frames.csv),
[updated scheduled](streaming-measurements/after-frames.csv),
[original synchronous](streaming-measurements/before-baseline.csv),
[updated synchronous](streaming-measurements/after-baseline.csv).

All 21 synchronous scenarios match exactly in operations, upload bytes, peak
resident chunks/sections, peak GPU bytes, materialized columns and section builds:
65,438 operations = 4,299 materializations + 61,139 builds across each capture.
Scheduled stationary scenarios perform zero materializations/builds/uploads in
both versions. The queue peaks at 1,681 in both; all residency assertions and
subsequent convergence drains pass.

| Scheduled measurement | Original | Updated |
| --- | ---: | ---: |
| Radius 25 stationary sync p95 (ms) | 0.250 | 0.196 |
| Radius 100 stationary sync p95 (ms) | 0.228 | 0.230 |
| Radius 300 stationary sync p95 (ms) | 0.933 | 0.959 |
| Radius 300 teleport sync p95 (ms) | 4.534 | 4.492 |
| Radius 300 teleport request loading (frames) | 567 | 520 |
| Radius 300 radius-expansion loading (frames) | 526 | 551 |

These are single serial observations, not a performance improvement claim.
Soft deadlines change how much work completes within the measured 240 frames:
scheduled operation totals were 19,542 versus 19,939, followed by complete
convergence drains. Collection/metadata overhead and hundreds of loading frames
at radius 300 remain visible in both versions; tuning those costs is deferred.
