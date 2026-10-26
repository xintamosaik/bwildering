# Investigate surface-height consistency before and after materialization

Priority: P1 — proposed correctness investigation

Status: Complete

Source: [Code review, 26 October 2026](../review.md) — Review §3 A.

## Problem

`world_surface_height()` combines procedural terrain with allocated edits in an unmaterialized chunk. The review suspects interactions between removed topmost blocks, higher additions and edited sections separated by synthetic sections. No failure has been established.

## Work

- Create streamed terrain and remove several topmost blocks without materializing the whole chunk.
- Compare surface height before and after materialization.
- Repeat with an added block above the procedural surface and edits in separated vertical sections.
- Fix any confirmed inconsistency while preserving allocation-failure behaviour.

## Acceptance criteria

- [x] Targeted tests prove surface-height queries agree before and after materialization for each scenario.
- [x] Tests cover negative section coordinates and separated edited sections.
- [x] Any reproduced defect is fixed; if none reproduces, record that result and retain the invariant tests.

## Order and dependencies

Run early, alongside streaming work. The review describes a hypothesis, not a confirmed bug.

## Implementation and evidence

No inconsistency reproduced; `world_surface_height()` required no production change.

- `tests/test_game_logic.c`: `test_surface_height_survives_materialization`
  checks positive and negative surface sections in negative horizontal chunks,
  with a translated origin and non-unit block size. Each fixture removes three
  topmost blocks, removes across two section boundaries, adds a higher block
  alongside a separated underground edit, removes that addition leaving an
  empty marker, and empties the entire column. Queries match explicit expected
  heights (or NaN) before materialization and agree afterward. Assertions verify
  intervening procedural sections remain unallocated and queries preserve
  resident counts and the chunk's unmaterialized state.
- `tests/test_allocation_failures.c`: materialization failure injection now
  compares surface heights across the region against the materialized reference
  after every rejected allocation and successful retry. Fixtures include both
  a higher addition and a separate column with removed surface blocks and a
  deep edit separated by procedural sections.
- Validation: `make test` and `ASAN_OPTIONS=detect_leaks=0 make test-sanitize`.
  The default sanitizer run stops because LeakSanitizer cannot operate under
  this environment's process tracing; the rerun retains AddressSanitizer and
  UndefinedBehaviorSanitizer coverage.
