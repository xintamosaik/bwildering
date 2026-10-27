# Greedy meshing evaluation

Priority: deferred until streaming and storage stabilize. Status: planned evaluation.
Related: [issue 009](../../issues/009-evaluate-greedy-meshing.md).

## Goal

Reduce coplanar face geometry only if the memory/upload/render savings justify
mesh-build cost and complexity. The existing culled-face mesher remains the
reference and fallback during evaluation.

## Implementation

1. Measure current faces, vertices, CPU build time, scratch peak, upload bytes
   and draw cost on flat terrain, mountains, caves, alternating materials and
   repeated edits. Use the stabilized streaming route as well as microbenchmarks.
2. Prototype six directional face masks using fixed-capacity scratch storage.
   Merge rectangles only when orientation, plane and material/render attributes
   match. Keep merges inside one section initially; reuse neighbor caching and
   preserve explicit material ranges.
3. Emit correctly wound triangles and define scratch/vertex capacity failures
   before replacing an existing mesh. Keep CPU edits authoritative and old GPU
   geometry available on replacement failure.
4. Compare surface coverage by expanding merged quads into unit faces for the
   independent face oracle. Exact old vertex bytes/counts are no longer the right
   correctness criterion for a geometry-reducing algorithm. Keep exact checks
   where they still protect unchanged formats or deterministic output.
5. Evaluate section seams and T-junction visibility on the real renderer,
   including distant views and compact vertex formats if adopted. Record an
   adopt/defer decision; avoid cross-section merging in the initial scope.

## Validation and completion

Test all materials/directions, empty/uniform/dense storage, checkerboard worst
cases, negative coordinates, procedural boundaries, hidden-section exposure,
removal/restoration and repeated eviction/revisit. Run sanitizer, failure and GL
coverage checks. Report geometry reduction, build cost, peak temporary storage
and travel/frame impact. Adoption requires equivalent visible surfaces and
material boundaries plus a measured benefit on representative workloads.
