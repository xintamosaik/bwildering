# Profile mesh neighbor queries and remove measured repeated work

Priority: P2

Status: Complete (26 October 2026)

Source: [Code review, 26 October 2026](../review.md) — Review §2: linked subchunks and terrain sampling; §5 item 3.

## Problem

Section meshing repeatedly calls `world_get_block()`. Linked section lookup and procedural noise sampling for unloaded neighbors may repeat expensive work. The review recommends measurement before changing storage or lookup structures.

## Work

- Benchmark representative materialized and synthetic-neighbor mesh builds.
- Measure total build time, neighbor-query time, linked-list traversal and terrain-column sampling counts/cost.
- If sampling dominates, obtain neighbor section/column data once per build and reuse it.
- If traversal dominates, evaluate direct indexing for the normal 32-section terrain range with sparse storage for out-of-range edits.
- Record findings and implement only optimizations supported by measurements.

## Acceptance criteria

- [x] A reproducible benchmark identifies the actual costs and baseline timings.
- [x] Any implemented caching preserves synthetic terrain and edit semantics.
- [x] Any storage change preserves sparse edits at arbitrary elevations.
- [x] Before/after results and correctness checks accompany changes; deferred options have a recorded rationale.

## Order and dependencies

After the streaming baseline in issue 001; inform storage decisions in issue 006.

## Resolution

Implemented build-local neighbor section/column reuse and a per-voxel face mask.
[Measurements and correctness checks](../mesh-query-performance.md) include a
reproducible frozen-baseline comparison, profiling overhead limits, the sparse
edit tradeoff and the rationale for deferring direct indexing. Section storage
and arbitrary-elevation sparse edits are unchanged.
