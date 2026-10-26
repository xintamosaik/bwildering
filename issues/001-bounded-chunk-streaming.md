# Finish bounded chunk residency and coordinated eviction

Priority: P1

Status: Complete

Source: [Code review, 26 October 2026](../review.md) — Review §2: materialization and eviction; §5 item 1.

## Problem

`world_materialize_chunk()` and `world_release_chunk()` provide lifecycle operations, but the review calls for a predictable policy deciding when chunks enter and leave memory. Keeping every edited chunk resident can grow memory indefinitely.

## Work

- Define radius-based residency around the player and ownership of residency decisions.
- Distinguish procedural baseline, player modifications, resident CPU voxels and resident GPU meshes.
- Coordinate CPU eviction and GPU mesh retirement, including stale or pending mesh work.
- Document the initial edited-chunk retention policy and its memory limits; track persistent edit storage separately if needed.
- Benchmark travel through the world, recording materialization time, resident chunks/sections, RAM and GPU mesh retirement.

## Acceptance criteria

- [x] Moving away from unedited terrain retires its CPU and GPU resources predictably.
- [x] Revisiting regenerated terrain produces the same baseline terrain.
- [x] Edits survive every supported residency transition; any limitation on edited-chunk eviction is documented.
- [x] A reproducible movement benchmark reports resource counts and timings, and demonstrates a bounded working set for unedited terrain.

## Order and dependencies

Start here. Coordinate with issues 002 and 010.

## Implementation and evidence

See [the residency policy and travel measurements](../streaming.md) and
`make streaming_capture`. Residency decisions belong to `renderer_sync_view()`;
CPU retirement tracks materialized bounds independently of GPU records and
visits only departing strips; stationary frames skip the eviction scan. Edited CPU
chunks remain resident; departing GPU resources retire even after failed uploads.
Persistent edit storage is tracked in [011](011-persistent-edit-storage.md).
Renderer regression coverage includes metadata allocation failure, upload failure,
edited revisits, full departure, radius reduction and deterministic regeneration.
The captured unedited route peaked at 36 resident chunks (limit 49), and ended
with zero resident CPU sections and GPU geometry.
