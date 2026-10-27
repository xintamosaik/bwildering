# Edit overlay and independent terrain eviction

Priority: after the streaming baseline. Status: planned.
Related: [issue 011](../../issues/011-persistent-edit-storage.md).

## Current behavior and goal

`Chunk.edited` permanently prevents `world_release_chunk()` from evicting a
modified procedural column. Even restored edits retain generated sections.
Separate seed-derived terrain, authoritative modifications and resident caches
so a small change retains only its edit data when the player leaves.

## Implementation

1. Define an overlay entry as signed voxel identity plus replacement material.
   Missing entry means procedural baseline; explicit empty means removal. Compare
   writes against the baseline, not the currently cached value. A write restoring
   baseline deletes its entry. Outside generated elevations, baseline is air.
2. Choose a bounded world-owned pool and per-chunk sparse index. Derive initial
   capacities and expected overhead from an edited-travel fixture. Document a
   maximum retained-edit count and clear exhaustion result. This bounds memory,
   not the amount of editing a player can perform indefinitely.
3. Introduce one read precedence contract: resident authoritative data when valid,
   otherwise overlay over procedural baseline. Audit `world_get_block()`,
   `world_surface_height()`, mesher temporary sections and boundary caches,
   collision and picking. Avoid materializing chunks just to read edits.
4. Make edit mutation transactional: reserve overlay and resident expansion
   capacity before publishing either change. On failure preserve values, counts,
   links, dirty flags and overlay ownership. Identical writes remain no-ops.
5. Apply overlay entries while staging chunk materialization, before publishing
   sections or mesh-candidate metadata. Include high/low edits, empty sections
   and neighbors exposed by removals. Track overlay presence separately from
   residency; replace the coarse permanent `edited` policy carefully.
6. Allow edited CPU columns to evict once the overlay is authoritative. Retire GPU
   resources and invalidate scheduled work before recycling CPU storage. On
   revisit reconstruct seed plus overlay exactly. Audit voxel/section counters:
   resident counts must not accidentally include unloaded overlay entries.
7. Keep session-local overlay implementation separate from disk persistence.
   Document edits as lost on world destruction until save/load ships. A later
   persistence phase must specify seed and generator-format version, signed
   coordinates, validation limits, explicit removals, atomic save replacement
   and failure behavior before supporting reload or spilling entries to disk.

## Validation and completion

- Compare eager and streamed reads, surface heights and reference faces before
  eviction and after repeated reloads. Cover negative coordinates, all materials,
  multiple removals, restored baseline, separated vertical edits and edit limits.
- Inject failure at each reservation/materialization step and exhaust the overlay
  pool. Rejected edits preserve both resident state and authoritative entries.
- Test edited travel across more chunks than fit in residency. Generated CPU/GPU
  storage remains bounded; overlay memory follows actual differences up to its
  documented capacity, and baseline restoration reclaims entries.
- Run shared storage/lifecycle checks and GL revisit tests. Update `streaming.md`,
  `game_logic.h` ownership contracts and issue 011 with measurements and limits.
- Complete the in-memory phase only when edited columns actually evict and edits
  survive reconstruction. Do not mark disk persistence complete without save/load
  implementation and interrupted-write/malformed-input tests.
