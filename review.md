# Code review — Bwildering, 26 October 2026

Overall assessment: The architecture is moving in the right direction. You're no longer building a collection of independently positioned cubes. You have a voxel world with spatially organized storage, deterministic terrain generation, section-level meshing and explicit resource ownership.

More importantly, your code increasingly reflects the engineering philosophy you've been aiming for:

- TigerBeetle: explicit invariants, failure handling, deterministic behaviour and transactional updates.
- id Software (Doom–Quake 3): specialized data structures, spatial partitioning, visibility-driven rendering and measurable performance.
- Casey Muratori: straightforward C, understandable execution paths, explicit data transformations and relatively little abstraction.

However, I see some important architectural limitations, particularly around memory allocation, terrain representation and the distinction between world data and rendering data.

## 1. What's working well

| Area                               | Assessment                           |
| ---------------------------------- | ------------------------------------ |
| Chunk/subchunk organization        | Good foundation                      |
| Signed voxel coordinates           | Good, including negative coordinates |
| Deterministic terrain sampling     | Good                                 |
| Surface-only mesh generation       | Correct direction                    |
| Dirty subchunk tracking            | Good                                 |
| Transactional CPU mesh replacement | Particularly good                    |
| Allocation-failure testing         | Excellent direction                  |
| Memory locality                    | Still needs improvement              |
| Large-world scalability            | Not there yet                        |
| Separation of responsibilities     | Improving, but incomplete            |

The strongest architectural decision is that the voxel world is authoritative, while the mesh is derived data.

That's precisely what you want for a Minecraft-style game:

```
Voxel coordinates + block types
              |
              v
       Chunk / Subchunk
              |
              v
        Surface builder
              |
              v
         GPU geometry
```

Changing a block changes world data. The relevant surfaces are then regenerated.

That design can support dynamic blocks without requiring the world to know anything about OpenGL.

## 2. Main concerns, ranked by priority

### P1 — Your terrain storage is still too expensive

`voxel_world.c`, `world_materialize_chunk()`

You allocate an entire `Subchunk` for each vertical section containing terrain, including sections completely filled with stone.

Each subchunk contains:

```
uint8_t blocks[SECTION_VOLUME];
```

That's 4,096 bytes for 4,096 blocks, plus metadata and allocation overhead.

APPROXIMATE VOXEL STORAGE

# 4 KiB

One subchunk

# 80 KiB

20 subchunks

# 5 MiB

8×8 chunks

Illustrative figures for block arrays alone, excluding mesh storage, allocations and chunk metadata.

The problem isn't the 4 KiB itself. It's allocating it when all 4,096 blocks are identical.

Consider three storage states:

```
typedef enum {
    SECTION_EMPTY,
    SECTION_UNIFORM,
    SECTION_DENSE
} SectionStorage;
```

A uniform stone section needs only a material identifier. A dense section needs the full array.

My recommendation: Keep your existing dense representation for now, but make uniform sections your next memory optimization once streaming works reliably.

This is especially relevant to your mountainous terrain, where many underground sections contain mostly homogeneous material.

### P1 — Materialization and eviction need to become a complete lifecycle

`world_materialize_chunk()` and `world_release_chunk()` give you the beginnings of this.

The important remaining question is: who decides when a chunk enters or leaves memory?

You want one clear lifecycle:

You've documented that edited chunks remain resident, while unedited chunks can be regenerated. That's reasonable as an initial policy, but eventually an explored world containing many edits will accumulate resident chunks indefinitely.

I'd distinguish:

- Procedurally generated baseline terrain.
- Player modifications.
- Currently resident voxel storage.
- Currently resident GPU meshes.

You don't need a sophisticated paging system yet. What you do need is a predictable radius-based residency policy, with CPU and GPU retirement coordinated.

### P2 — Linked subchunks are simple but not especially cache-friendly

Your chunks contain:

```
Subchunk *sections;
```

And the sections form a linked list sorted by vertical coordinate.

I understand why: this accommodates sparse vertical storage and arbitrary edit elevations without allocating a massive vertical array.

But each lookup potentially traverses several allocations. During mesh generation, you call `world_get_block()` for neighboring voxels repeatedly.

For a 16³ section, that's potentially 24,576 neighbor queries before accounting for the other passes.

Because you have a fixed normal terrain range of 32 sections, a direct indexed representation would be worth considering, with separate sparse storage for edits outside that range.

I would not rewrite this immediately. First measure how much time is spent in neighbor lookups. Linked lists are not automatically a problem at 10–30 nodes, but they are not a natural endpoint for an engine seeking high throughput.

### P2 — Your vertex representation repeats world-space coordinates

In `section_mesh_build()` you emit vertices like:

```
world->origin.x + (wx + corner[0]) * world->blockSize
```

Every vertex stores absolute world coordinates.

For a section with many exposed faces, this repeats large quantities of position information that could be derived from its section origin.

You already know a mesh belongs to `(chunkX, sectionY, chunkZ)`.

Eventually, consider generating local coordinates within a section and applying a section transform during rendering.

Advantages include smaller possible vertex formats, better precision for distant chunks and simpler mesh relocation.

However, your present three-float vertex representation is easy to understand and validate. I'd measure GPU bandwidth and vertex-memory usage before replacing it.

### P2 — Your terrain-generation sampling can become expensive

`world_get_block()` synthesizes unmaterialized terrain using `terrain_sample_column()`.

That's useful for correctness: querying unloaded terrain doesn't necessarily require a chunk allocation.

But `terrain_sample_column()` evaluates several noise layers. If a mesher repeatedly queries an unloaded neighboring chunk, you're potentially repeating expensive terrain calculations.

You already cache columns during chunk materialization. Good.

The next optimization should be to obtain neighbor section or column data once per mesh build, rather than going through generic world lookup for every exposed face.

This gives you a cleaner hot path without introducing a complex abstraction hierarchy.

## 3. Two concrete correctness issues to investigate

### A. `world_surface_height()` may ignore blocks above a synthesized terrain surface

In an unmaterialized streamed chunk, it starts with the procedural surface, works downward through allocated edits, and then searches allocated sections for higher blocks.

That is sensible overall, but the search in the procedural branch needs particular scrutiny when the highest procedural blocks have been deleted, or when there are multiple edited sections separated by still-synthetic sections.

Test this sequence:

1. Generate a streamed chunk.
2. Remove several topmost blocks without materializing the whole chunk.
3. Query the surface height.
4. Materialize the chunk.
5. Query the surface height again.

The two values should agree. Repeat with an added block above the procedural surface.

I haven't established a definite failure from the code alone, but this interaction deserves a targeted invariant test.

### B. Section exposure and mesh-candidate classification must stay consistent

You use:

```
if (!section || section->voxelCount == 0 ||
    (!section->meshCandidate && !editedNeighbor))
```

This optimization assumes that a noncandidate section cannot expose geometry unless an appropriate edit has occurred.

The classification in `world_materialize_chunk()` is conservative, which is good.

But I would specifically test an initially hidden section becoming exposed after edits to an adjacent section, including across chunk boundaries and at negative Y coordinates.

A brute-force reference mesher for small test worlds would be valuable here. It can compute visible faces without any `meshCandidate` optimization and compare them with your optimized output.

## 4. Where your engineering philosophies stand

| Philosophy     | What you're applying well                                       | What needs attention                              |
| -------------- | --------------------------------------------------------------- | ------------------------------------------------- |
| TigerBeetle    | Failure injection, transactional generation, explicit contracts | More state-transition invariants                  |
| Doom / Quake   | Spatial organization, surface geometry, visibility optimization | Cache behaviour, residency and draw submission    |
| Casey Muratori | Direct code, few layers, concrete data structures               | Avoid scattered special cases and duplicated work |

The allocation-failure test suite particularly impressed me. You're testing that failed operations preserve existing data and object ownership rather than merely checking return codes.

One potential concern is defensive-code accumulation. There are quite a few repeated bounds, floating-point and state checks distributed throughout the implementation.

Defensive validation is appropriate at public boundaries. But in performance-critical internal functions, you should eventually make stronger guarantees upstream rather than repeatedly revalidating everything.

This is a design choice to revisit, not an instruction to remove safety checks prematurely.

## 5. What I would do next

In order:

1. Finish and benchmark chunk streaming. Measure materialization time, section count, RAM consumption and GPU mesh retirement while moving through the world.
2. Add lifecycle invariant tests. Verify that materialize → edit → rebuild → revisit preserves exactly the expected world state.
3. Benchmark mesh neighbor queries. Determine whether noise sampling and linked-list traversal are genuine bottlenecks.
4. Introduce uniform subchunk storage. This is likely to deliver substantial memory savings for underground terrain.
5. Only then consider greedy meshing. Your current face-culling approach is a solid baseline. Greedy meshing can considerably reduce geometry, but introduces its own complexity, especially at material boundaries.

I would not introduce an ECS, a generalized resource manager, a job graph or an elaborate renderer abstraction at this stage. None is justified by the demonstrated requirements.

## 6. Review limitations

The uploaded snapshot contains `game_logic.c`, `voxel_world.c`, the headers, Makefile and several tests, but not `main.c`.

Your renderer tests include `main.c` directly, so the snapshot gives evidence of the testing strategy, but not enough to independently review the actual renderer synchronization, OpenGL resource lifecycle, frustum culling or frame-time budgeting. I also haven't compiled or executed the tests.

That limits my confidence in the end-to-end streaming behaviour.

## Verdict

Your strongest improvement is conceptual: you now have a world representation that can exist independently of the geometry used to render it.

The next architectural milestone isn't more abstraction. It's establishing a bounded, measurable working set of chunks and subchunks.

That would bring you closer to the common ground between TigerBeetle, classic id Software and Casey Muratori: systems whose data, ownership, resource costs and state transitions are explicit and understandable.

My priority would be streaming correctness first, memory efficiency second, and mesh sophistication third.