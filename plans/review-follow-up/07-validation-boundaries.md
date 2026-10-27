# Validation boundaries

Priority: deferred, after hot-path measurements. Status: planned evaluation.
Related: [issue 008](../../issues/008-validation-boundaries.md).

## Goal

Remove repeated work only where an upstream contract demonstrably guarantees
the same condition. Keep defined public-input behavior and resource-failure
handling. No broad safety-check removal is planned.

## Implementation

1. Profile current mesh, collision and streaming paths and list repeated checks
   with measurable costs. Identify public entry points and every caller of each
   candidate internal helper.
2. Write a small contract table: input range/state, where it is established,
   whether it survives callbacks or lifecycle transitions, and failure behavior.
   Pay particular attention to queued work invalidated by movement or edits.
3. Consolidate checks at a validated outer loop or introduce a narrowly scoped
   internal helper with explicit preconditions. Use assertions for internal
   programmer errors where appropriate, while preserving required release-build
   handling of external input, capacity exhaustion and allocation/GL failure.
4. Change one measured path at a time. Compare optimized builds under the same
   workload and retain existing checks if the improvement does not justify the
   added precondition burden.

## Validation and completion

Exercise invalid types, signed bounds, NaN/infinite coordinates, lifecycle
transitions and failure injection at public boundaries. Run shared checks and
document each changed helper's audited callers. Complete with an adopt/defer
record containing the contract and measured gain; no speculative cleanup.
