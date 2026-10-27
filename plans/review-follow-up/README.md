# Review follow-up implementation plans

Created 27 October 2026 after comparing [the review](../../review.md) with the
current implementation. Plans 01, 03 and 04 are now implemented and validated;
their completion evidence and fresh measurements are linked there.

## Scope and order

| Order | Plan | Decision |
| --- | --- | --- |
| 1 | [Streaming measurements and frame work budget](01-streaming-budget.md) | Implemented: bounded scheduled work; measurements and completion evidence in the plan. |
| 2 | [Edit overlay and independent terrain eviction](02-edit-overlay.md) | Preserve modifications without pinning generated columns. |
| Alongside 1–2 | [Renderer verification](03-renderer-verification.md) | Verified on accelerated OpenGL; ownership, depth/submission and staged convergence evidence in the audit. |
| Measure alongside 1; implement after contracts settle | [Allocation accounting and reusable storage](04-allocation-storage.md) | Implemented: measured churn, separate pools, upload scratch and reusable renderer capacity; evidence in the plan. |
| Deferred | [Section-local and compact vertices](05-local-vertices.md) | Adopt only for measured memory, rendering or precision benefits. |
| Deferred | [Direct section indexing](06-section-indexing.md) | Reconsider only if lookup costs become material. |
| Deferred | [Validation boundaries](07-validation-boundaries.md) | Consolidate only demonstrated redundant hot-path checks. |
| Deferred | [Greedy meshing](08-greedy-meshing.md) | Evaluate after streaming and memory changes stabilize. |

Uniform storage, mesh-neighbor reuse, lifecycle invariants, surface-height
consistency tests and hidden-section reference tests already exist. Preserve
them rather than repeating the original review's implementation sequence.
See [the issue tracker](../../issues/README.md) for their evidence.

## Shared implementation requirements

- Prefer fixed-capacity storage, pools and arenas with explicit owners and lifetimes.
  Define exhaustion behavior before replacing existing allocation paths.
- Use const-correctness and explicit, range-justified numeric conversions.
- Check every changed C file with Clang-Tidy's
  `bugprone-narrowing-conversions`, using the actual build flags; resolve warnings
  introduced by the change.
- Preserve immediate authoritative block edits, procedural determinism,
  transactional section replacement and retryable failure behavior.
- Run `make test` and `make all` for implementation changes. Run
  `make test-sanitize` for storage/lifecycle changes; document any required
  LeakSanitizer exclusion separately from ASan/UBSan results.
- Run `make test-renderer` for rendering or GPU lifecycle changes. A successful
  build is not a successful GL test. Record unavailable display/context checks.
- Compare timings serially under the same build, route and device. Record seed,
  compiler, hardware and instrumentation settings; do not impose machine-specific
  timing thresholds in correctness tests.

At plan creation, `make test` and the application build passed. The renderer
suite compiled but could not initialize X11 display `:0`. Existing documented
streaming timings predate later optimizations and need refreshing.
