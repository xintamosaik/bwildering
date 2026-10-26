# Verify hidden sections become mesh candidates after neighboring edits

Priority: P1 — proposed correctness investigation

Status: Complete — deterministic reference comparisons pass; no defect demonstrated

Source: [Code review, 26 October 2026](../review.md) — Review §3 B.

## Problem

The mesher skips a section when it is not a mesh candidate and has no qualifying edited neighbor. This optimization must remain consistent with actual exposure after edits, including across chunk boundaries and at negative Y. The review does not establish a failing case.

## Work

- Build small worlds containing initially hidden sections.
- Expose them by editing adjacent sections, including across chunk boundaries and at negative Y.
- Add a simple reference face enumerator that ignores `meshCandidate` classification.
- Compare optimized output with reference visible-face positions, orientations and materials.

## Acceptance criteria

- [x] Optimized and reference visible faces match before and after neighboring edits.
- [x] Tests cover vertical section boundaries, chunk boundaries and negative Y.
- [x] Dirty propagation schedules affected sections for rebuilding.
- [x] Any demonstrated classification or invalidation defect is fixed (none found).

## Verification

`test_hidden_section_neighbor_edits` in `tests/test_game_logic.c` uses seed 1
in a 96-block world. Interior sections at Y = -6, -1 and 0 start hidden and
are exposed from each of their six sides. The reference enumerator reads block
values without consulting occupancy, candidate or edited-chunk flags, and
compares owning voxel, outward winding and material against emitted geometry.
It checks removal, every exposed-face material, and restoration.

Fixtures cover eager terrain, fully resident streamed terrain, a resident target
with procedural neighbours, and edits before target materialization. Exact dirty
section identities are checked on removal and restoration; materialization must
also schedule newly resident targets. The last fixture verifies the edited-chunk
fallback when materialization leaves an exposed target's candidate flag false.

Run with `make test` and `make test-sanitize`.

Validation passed: `make test` and
`ASAN_OPTIONS=detect_leaks=0 make test-sanitize`. LeakSanitizer cannot run under
this environment's tracing; ASan and UBSan remained enabled. Temporary variants
with the edited-neighbour fallback disabled or neighbour dirty propagation
disabled were both rejected by the new regression test.

## Order and dependencies

Run early. Use small deterministic fixtures so the reference remains straightforward.
