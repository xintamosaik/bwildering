# 011: Persistent edit storage

Status: in-memory overlay complete; disk persistence open.

Procedural edits now survive CPU/GPU eviction in a session-local world overlay.
The pool holds at most 65,536 actual differences, including explicit removals;
restoring the procedural baseline reclaims an entry. The lazily allocated arena
owns 1,310,720 bytes on the measured build (20 bytes per signed voxel entry).
Exhaustion rejects new differences without changing resident or overlay state.
The 1,024-column travel fixture fills the pool and reconstructs each column twice
while keeping only one column resident. See [streaming.md](../streaming.md) for
read precedence, ownership, measurements and validation.

Edits are lost on world destruction. Before implementing save/load or spilling
entries to disk, specify and validate:

- World seed and generator-format version compatibility.
- Signed voxel coordinates, replacement materials and explicit air removals.
- Bounds, entry-count limits, duplicate handling and malformed-input rejection.
- Atomic save replacement and behavior on write failure or interruption.
- Transactional load failure that preserves the current world.

Completion requires save/load round trips, malformed-input tests, interrupted
write tests and documented failure behavior. No disk persistence is implemented.
