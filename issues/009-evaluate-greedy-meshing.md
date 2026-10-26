# Evaluate greedy meshing after establishing streaming and memory baselines

Priority: P3 — deferred evaluation

Status: Open

Source: [Code review, 26 October 2026](../review.md) — Review §5 item 5.

## Problem

Face culling is the current baseline. Greedy meshing may reduce geometry, but introduces complexity at material boundaries and should follow streaming correctness and memory improvements.

## Work

- Record face/vertex counts, build time and mesh memory for representative terrain.
- Evaluate merging adjacent coplanar faces with compatible material/rendering attributes.
- Compare geometry reduction against mesh-build cost and implementation complexity.
- Keep this as an evaluation until measurements justify adoption.

## Acceptance criteria

- [ ] A benchmark-backed adopt/defer decision is recorded.
- [ ] If implemented, visible surface coverage, orientation and material boundaries match the reference.
- [ ] If implemented, edits and section/chunk seams remain correct.
- [ ] Before/after geometry counts, mesh memory and build times are reported.

## Order and dependencies

After 001–006, especially reliable streaming, neighbor profiling and uniform storage.
