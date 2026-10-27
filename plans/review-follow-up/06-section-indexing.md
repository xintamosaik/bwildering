# Direct section indexing

Priority: deferred, measurement-gated. Status: planned evaluation.
Related: [issue 005](../../issues/005-mesh-neighbor-query-performance.md).

## Goal and decision gate

The mesher now resolves only seven sections per build. Existing measurements do
not justify replacing sparse sorted lists. Reconsider indexing only if profiling
after the planned changes shows meaningful remaining lookup costs, including
collision and picking rather than only mesh construction.

## Implementation

1. Refresh `mesh_query_benchmark` and `mesh_query_profile` serially. Add relevant
   collision/edit fixtures with deep columns and sparse high/low sections. Report
   calls, traversed links, total operation time and index memory cost per chunk.
2. If costs justify a prototype, add a borrowed pointer table for the normal 32
   generated Y sections and keep sparse fallback for out-of-range edits. Define
   the sorted list/pool as owner; the index must never own or free section nodes.
3. Populate index entries only on transactional commit and clear them before
   eviction/reuse. Audit insertion, materialization rollback, empty markers,
   overlay reconstruction and world destruction. Account for all chunk metadata,
   especially if allocating a table for every world chunk instead of residents.
4. Compare the extra memory and invalidation work with end-to-end gains. Retain
   the current representation if the benefit is negligible.

## Validation and completion

Check indexed reads against list lookup across all normal Y indices, both sparse
limits, clipped negative chunks and failure/retry. Run lifecycle and sanitizer
checks if adopted. Record an adopt/defer decision with timings and storage costs;
do not rewrite the structure solely because linked lists are unfashionable.
