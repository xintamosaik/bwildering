# Allocation accounting and reusable storage

Priority: measure with plan 01; migrate after lifecycle contracts settle.
Status: implemented and validated (27 October 2026).

## Behavior at planning and goal

Uniform storage reduced live payload memory, but individual section nodes,
dense payloads and CPU meshes still allocate separately. Renderer metadata also
allocates a replacement array when identities change. Measure churn and replace
costly paths with explicit-capacity storage while preserving rollback behavior.

## Implementation

1. Add opt-in benchmark accounting for allocation attempts, requested/live/peak
   bytes, capacity waste and storage reuse by category. Include section nodes,
   dense payloads, CPU meshes, renderer records and index arrays. Keep allocator
   overhead, RSS and requested GPU bytes distinct. Do not add clocks to every
   production allocation or instrument GL internals as if they were owned bytes.
2. Capture stationary convergence, cold travel, normal movement, repeated edits,
   overlay growth, maximum-radius residency and failure/retry. Determine which
   allocations repeat and how much temporary storage overlaps during replacement.
3. Size separate reusable pools for section nodes and 4 KiB payloads. Retain sparse
   signed section links initially. Choose bounded capacity or explicitly bounded
   arena reservation from supported residency plus edit-overlay requirements;
   document initialization cost and exhaustion behavior rather than reserving
   an unexplained worst-case amount.
4. Provide scoped scratch storage for mesh construction. Derive safe worst-case
   vertex counts from section dimensions and face rules, checking size arithmetic
   before reservation. Keep the public owning `SectionMesh` API where required;
   add an explicit borrowed/scratch build path for immediate upload rather than
   passing scratch memory to `section_mesh_destroy()`.
5. Reuse renderer metadata with capacity-managed staging storage or two buffers.
   Preserve stable identity merging and one owner per GL handle. Account for old
   and new arrays coexisting during a transactional replacement. Reuse visible
   and draw-index capacity without shrinking every frame.
6. Migrate one ownership category at a time. Reset scratch only after its final
   consumer; do not return pooled nodes while scheduled tasks can reference them.
   Replace malloc-only fault injection with pool/arena reservation injection so
   failure coverage remains meaningful.

## Validation and completion

- Test pool exhaustion, reuse after eviction, scratch overflow, partial staging
  failure and successful retry. Check no aliasing or stale data across worlds.
- Compare exact block values and mesh output with the existing paths. Run shared
  sanitizer and GL ownership checks after each migration.
- Report before/after allocation counts, requested/live/peak capacity, timing
  distributions and initialization cost. A stationary converged frame should
  allocate nothing; repeated routes should reuse storage within configured bounds.
- Complete only migrations supported by measurements; record deferred categories
  and the capacity/failure contract for every adopted pool or arena.

## Completion evidence

See [allocation-storage.md](../../allocation-storage.md) for the adopted capacity
and failure contracts, diagnostic definitions, reproducible before/after captures,
initialization cost, retention tradeoffs and validation. Separate lazily committed
node/payload pools, borrowed immediate-upload scratch and reusable renderer
metadata/index capacity are implemented. Fault injection now rejects logical
reservations, including reuse. Public owning meshes, chunk metadata and the edit
arena retain their existing lifetime contracts; their disposition is explicit in
the report. No GPU allocator overhead or frame-time bound is inferred from the
requested-byte counters.
