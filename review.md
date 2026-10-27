# Code review — Bwildering, 27 October 2026

Overall assessment: The architecture is substantially stronger than in the previous review. The most valuable improvements are that you've optimized a measured bottleneck, introduced a compact storage representation, and retained relatively simple ownership rules.

I reviewed the supplied implementation, benchmark reports, tests, and issue tracker. I haven't independently compiled or executed the project, and the supplied snapshot excludes `main.c`, so renderer integration cannot be fully verified here.

## 1. What has improved

| Area                        | Assessment            | Reason                                              |
| --------------------------- | --------------------- | --------------------------------------------------- |
| Chunk/subchunk architecture | Good                  | Spatial organization and signed coordinates         |
| Empty/uniform/dense storage | Very good             | Explicit states and substantial measured savings    |
| Meshing performance         | Excellent progress    | Repeated neighbour queries eliminated               |
| Allocation failure handling | Strong                | Prepare before committing changes                   |
| Streaming                   | Functional foundation | Residency and GPU retirement have explicit policies |
| Testing                     | Strong                | Equality tests, lifecycle tests, failure injection  |
| Frame-time predictability   | Needs work            | Synchronous generation remains expensive            |
| Long-term edit storage      | Needs work            | Edited chunks accumulate indefinitely               |

The most important architectural quality is that the voxel data remains authoritative and meshes are disposable derived data.

You haven't accidentally built a framework around the problem. Most of the operations remain relatively direct.

## 2. Findings, in priority order

P1

Synchronous streaming creates substantial frame-time spikes

Your `streaming.md` measurements show mean synchronization of about 78 ms and a maximum around 195 ms during cold travel. These are stressful benchmark conditions, not normal walking frames, but they're significant.

The algorithm isn't fundamentally wrong. However, the game currently performs generation, meshing and upload synchronously. I'd make bounded work per frame the next performance objective, preferably beginning with a simple queue rather than an elaborate asynchronous system.

P2

Edited chunks remain permanently materialized

In `world_set_block()`, every successful procedural edit marks the chunk `edited`. In `world_release_chunk()`, edited chunks cannot be evicted.

This is correct under your current ownership policy, but one modification can retain an entire generated column. I'd eventually store modifications independently of regenerated baseline terrain. Issue 011 already identifies this.

P2

Section lookup is still linked-list based

This is not a recommendation to change it now. The neighbour-cache optimization reduced section lookups to seven per mesh build, so the previous justification for replacing the linked list has largely disappeared.

Keep the current representation until another profile demonstrates an actual problem. That's consistent with your performance-first approach.

P2

Mesh memory remains an important next measurement

`section_mesh_build()` emits six `SurfaceVertex` entries per visible face, each with three floats. That's 72 bytes per face before allocator and GPU overhead. The CPU mesh is temporary, but the GPU representation retains this cost.

Greedy meshing or indexed quads could reduce it considerably. I wouldn't implement both at once. Measure representative geometry first.

P3

The documentation is becoming stale

`review.md` still describes uniform storage and neighbour-query caching as future improvements. Both are implemented. Your issue tracker clarifies this, but the primary review document is no longer a reliable snapshot of current shortcomings.

I'd mark resolved observations as historical rather than repeatedly reviewing already-fixed problems.

## 3. Specific correctness observations

`world_set_block()` is particularly well structured. It prepares an expandable section before linking it into the world or changing voxel counts. That gives you meaningful failure atomicity, not just error checking.

I would still finish two pending investigations:

- Issue 003 — surface-height consistency: Compare surface queries before and after materialization with deleted surface blocks, added high blocks, and multiple edited sections.
- Issue 004 — mesh-candidate correctness: Compare optimized meshes against a simple face-by-face reference, especially when edits expose previously buried faces.

I also noticed that `world_surface_height()` accepts coordinates exactly on the upper world boundary and maps them to the final cell. That may be intentional, but it differs from the half-open bounds used elsewhere. I'd standardize or explicitly document that convention.

None of these observations establishes a critical corruption bug in the supplied code.

## 4. Alignment with your engineering influences

| Philosophy          | Current alignment                                                                                                 |
| ------------------- | ----------------------------------------------------------------------------------------------------------------- |
| TigerBeetle         | Strong: explicit ownership, transactional changes, deterministic operations, failure injection                    |
| id Software / Quake | Good: spatial organization, visible geometry, measured performance; predictable streaming still needs improvement |
| Casey Muratori      | Good: direct C code, limited abstraction, build-local caches, measurable optimizations                            |

The most compelling example is issue 005. You measured a concrete problem, changed the hot path without altering persistent world state, compared exact mesh output, and recorded before/after performance. The resident surface fixture improved from approximately 866 µs to 56 µs per build in the documented run.

That is much more valuable than redesigning the section lookup structure because a different data structure looks theoretically faster.

## 5. Recommended next steps

I'd keep the next iteration narrow:

1. Close issues 003 and 004 with targeted correctness tests.
2. Verify the complete renderer lifecycle under a working OpenGL environment, including edits and eviction.
3. Add instrumentation for the maximum work performed in a single frame during ordinary movement.
4. Introduce a bounded streaming-work queue only after measuring those frame-time costs.
5. Defer persistent edit storage, greedy meshing and section-local vertices until their costs become relevant.

One important design consideration: moving to a work queue introduces partially prepared states. Keep CPU terrain authoritative and publish completed meshes only. Avoid a proliferation of partially initialized objects and independent lifecycle flags.

## Verdict

The project is now at the point where I would stop pursuing broad architectural refactors.

You've established the essential Minecraft-like pipeline, optimized expensive neighbour queries, reduced voxel storage dramatically, and started enforcing lifecycle invariants.

The next challenge is predictable execution cost, rather than data structure sophistication.

The current architecture looks suitable for continuing development. I would not replace the sparse sections, introduce an ECS, or add a generalized job system at this stage.