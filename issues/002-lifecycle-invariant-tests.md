# Test world-state invariants across chunk lifecycle transitions

Priority: P1

Status: Complete

Source: [Code review, 26 October 2026](../review.md) — Review §4: state-transition invariants; §5 item 2.

## Problem

The allocation-failure tests are a strong foundation, but the review asks for explicit invariants across materialization, editing, rebuilding, eviction and revisiting.

## Work

- Exercise materialize → edit → rebuild → leave → revisit using the supported residency policy.
- Compare block data and derived geometry before and after transitions.
- Cover unedited regeneration and preservation of player modifications.
- Extend failure-injection coverage where new lifecycle transitions allocate resources.

## Acceptance criteria

- [x] Tests assert exact expected block values after rebuilding and revisiting.
- [x] Tests cover both retained edited chunks and regenerated unedited chunks.
- [x] Failed transitions preserve existing world data and resource ownership.
- [x] CPU/GPU ownership and dirty-state invariants are documented and asserted at meaningful transition boundaries.

## Order and dependencies

Develop alongside issue 001; reuse the existing failure-injection approach.

## Implementation and evidence

- `tests/test_game_logic.c`: `test_chunk_lifecycle_invariants` compares seeded
  terrain with an eager reference across materialization, surface removal,
  sparse placement, empty-marker retention, CPU rebuilds and repeated
  departure/revisit. It checks exact vertices and material ranges in both edited
  and unedited columns, block values, section identity, dirty flags and resident
  section/voxel accounting.
- `tests/test_allocation_failures.c`: rejects every discovered allocation during
  materialization with pre-existing edits and a resident neighbour, preserving
  section links, contents, counts, flags and materialized bounds. Regenerated
  CPU mesh failures preserve the previously owned mesh and permit retry.
- `tests/test_renderer.c`: extends the streamed retirement regression with live
  metadata/visible-index allocation failures, exact GL buffer readback and draw
  ranges, empty-marker retirement, failed-upload ownership, dirty-state checks
  and matching regenerated geometry after return.
- [Test contracts](../tests/README.md) document CPU/GPU ownership and the scope of
  transactional failure guarantees. The supported residency policy remains
  [edited CPU retention with GPU retirement](../streaming.md).

Validation: `make test`, `make test-renderer` (local X11/OpenGL display), and
`ASAN_OPTIONS=detect_leaks=0 make test-sanitize` passed. ASan/UBSan cover the
headless suites; LeakSanitizer cannot run under this environment's tracing.
