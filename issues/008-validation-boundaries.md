# Review repeated validation in performance-critical internal paths

Priority: P2 — proposed follow-up

Status: Open

Source: [Code review, 26 October 2026](../review.md) — Review §4: defensive-code accumulation.

## Problem

Bounds, floating-point and state checks are repeated throughout the implementation. The review suggests stronger upstream guarantees where justified, while retaining defensive validation at public boundaries.

## Work

- Identify repeated checks in measured hot paths.
- Document which invariants public entry points establish and internal functions require.
- Consolidate redundant checks only where callers demonstrably guarantee the invariant.
- Preserve failure handling and use assertions where appropriate for internal contracts.

## Acceptance criteria

- [ ] Any changed validation has a documented upstream guarantee and audited callers.
- [ ] Invalid public inputs continue to receive defined handling.
- [ ] Relevant boundary and failure-injection tests pass.
- [ ] Performance-motivated changes include evidence; retain checks when there is no justified improvement.

## Order and dependencies

Revisit after lifecycle contracts and profiling (002 and 005). Do not remove checks speculatively.
