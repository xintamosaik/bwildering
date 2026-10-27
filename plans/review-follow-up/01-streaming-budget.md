# Streaming measurements and frame work budget

Priority: first implementation work. Status: implemented and validated (27 October 2026).
Related: issues [001](../../issues/001-bounded-chunk-streaming.md) and
[010](../../issues/010-renderer-lifecycle-audit.md).

## Behavior at planning and goal

`application_frame()` calls `renderer_sync_view()` before rendering. That call
materializes the entire desired neighborhood and builds/uploads every dirty
resident section synchronously. Residency is bounded for unedited terrain, but
cold transitions can stall a frame. Preserve that ownership model while limiting
how much new work starts per frame.

## Implementation

1. Extend `tests/streaming_capture.c` with deterministic stationary, walking,
   running, teleport, reverse-direction and radius-change scenarios. Use fixed
   simulation steps and the game's movement speeds. Cover radii 25, 100 and 300,
   negative coordinates, mountains and world edges. Retain cold travel as a
   separate stress scenario.
2. Measure current materialization, meshing, upload submission, retirement and
   metadata costs separately. Report p50/p95/p99/max sync and full-frame times,
   work counts, bytes, residency and peak pending work. Keep GPU drain timing
   outside normal frame loops and label submission time separately from GPU work.
3. Split desired residency collection from work execution in `main.c`. Describe
   pending tasks by signed chunk/section identity and revision, not borrowed
   section pointers or record indices. Use a bounded queue owned by the renderer;
   calculate capacity from the supported residency bounds and define overflow
   behavior that defers/rescans work without losing dirty state.
4. Prioritize dirty edits and nearby missing geometry, with deterministic
   tie-breaking and aging so distant work cannot starve. Remove obsolete tasks
   after movement or radius shrink. Validate identity/revision before execution.
5. Add per-frame elapsed-time and operation/byte limits selected from the new
   baseline. Check limits before starting another atomic operation. Initial units
   are one transactional chunk materialization and one section build/upload.
   These are soft time limits: a single operation can exceed the deadline.
   Measure the worst atomic cost before promising a frame-time bound.
6. Keep successful GPU meshes drawable while their replacement is pending;
   newly requested sections remain absent until uploaded. Collision and block
   queries continue to use authoritative CPU/procedural data. Retire departing
   resources promptly; measure retirement separately because it is not made free
   by budgeting new work. Preserve a synchronous drain path for startup,
   deterministic captures and existing fixture tests.
7. Separate deferred progress from actual failure in the sync result contract.
   Queue pressure is normal progress, not application failure. Failed operations
   retain valid old geometry and dirty state; retry without duplicating work.
   Initially build and upload one section together so CPU mesh ownership does not
   cross frames. If an atomic operation remains too costly, plan incremental
   staging as a measured follow-up rather than silently breaking transactionality.

## Validation and completion

- Add deterministic tests using a work-count limit or injectable clock; avoid
  timing-dependent assertions. Verify eventual completion, prioritization,
  cancellation, bounded queue occupancy and no work after stationary convergence.
- Exercise edits while queued, leave/revisit before execution, allocation/upload
  failure, radius shrink and queue exhaustion. CPU values stay current; rendered
  geometry converges to the synchronous reference with matching materials.
- Run the shared checks and real GL lifecycle suite. Refresh `streaming.md` with
  comparable before/after results, worst atomic operation, convergence latency
  and remaining spikes. Record the selected budget and visual loading policy.
- Complete when normal movement performs bounded scheduled work, pending work
  cannot grow without limit or starve, and measured stalls improve without
  changing final world/mesh results. Do not claim a hard time guarantee.


## Completion evidence

Gameplay now uses a fixed-capacity renderer queue, revision validation, distance
and edit priorities with aging, rotating overflow admission, and soft elapsed,
operation and upload-byte budgets. Startup and fixtures retain synchronous drain.
Deferred progress is distinct from failure; retries preserve old drawable meshes
and authoritative CPU edits, including metadata-allocation failures.

Deterministic real-GL tests cover count/byte limits, nearest work, aging,
exhaustion/rescan, stationary convergence, revision refresh, edits and leave/revisit
while queued, radius shrink, metadata/upload failures, and converged GPU byte and
material-range equality with synchronous section builds. Shared checks,
ASan/UBSan (LeakSanitizer disabled under tracing), real GL lifecycle tests and
Clang-Tidy narrowing checks ran; no new narrowing warnings were introduced.

See [streaming.md](../../streaming.md#scheduled-frame-work-27-october-2026)
for budgets, visual loading policy, comparable captures, atomic maxima,
convergence latency and remaining unbudgeted retirement/metadata spikes.
The synchronous reference and scheduled captures use identical deterministic
simulation inputs and the same GL device. Atomic operations remain transactional;
no hard frame-time bound is claimed. Incremental materialization/meshing remains
a measured follow-up if individual operations become unacceptable.
