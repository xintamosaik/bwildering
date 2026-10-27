# Empty, uniform and dense section storage

Implemented for [issue 006](issues/006-uniform-section-storage.md).

`Subchunk` now has an explicit `SECTION_EMPTY`, `SECTION_UNIFORM` or
`SECTION_DENSE` state. Empty sections read as air, uniform sections carry one
material, and only dense sections own a 4,096-byte array. `section_block` is the
shared read path for lookups, surface height, meshing and its neighbor cache.
Section identity, signed sparse ordering, voxel counts, mesh candidacy and dirty
flags remain independent of storage state. Empty retirement markers remain
allocated so renderer synchronization can retire their previous geometry.

Generation stages one section on the stack, classifies it, and allocates a block
payload only if mixed. Chunk materialization still prepares all missing sections
before committing any. Destruction, eviction and failed temporary meshes free
both the node and any dense payload.

An edit expands empty/uniform storage by allocating and filling a private array
before changing counts, flags, links or chunk edit status. A new section is linked
only after this expansion succeeds. Allocation failure preserves the existing
world and neighbor flags. Removing the last voxel releases the dense payload;
a full dense section containing one material compacts back to uniform storage.
Identical writes allocate nothing and leave metadata unchanged.

## Memory experiment

Run `make section_storage_benchmark && ./section_storage_benchmark`. This headless
Linux/glibc fixture selects the highest sampled column on a 64-block grid in
[-1024,1024]² for each seed, then materializes its surrounding 128×128 region.
The sampled peaks are 357, 331 and 288 blocks: mountainous terrain with both
homogeneous deep sections and mixed surface/layer sections. It averages five
fresh generations and computes an FNV-1a checksum of every cell in the generated
vertical range, including air.

Before/after builds used GCC 16.2.1, `-O2 -std=c99`, x86-64, glibc 2.44 on an
Intel i5-6500. The old sources were saved before the change; the same fixture
compiled with `-DLEGACY_SECTION_STORAGE` against those sources. `Subchunk` changed
from 4,128 bytes to 48 bytes. World and chunk-grid metadata are included below.
Allocator totals sum `malloc_usable_size(pointer) + sizeof(size_t)` for every live
allocation, accounting for glibc arena chunk rounding and effective size-word
overhead. This is live allocation accounting, not RSS: free arena space, allocator
global metadata, fragmentation, stack staging and meshes are excluded.

| Seed | Region center X/Z | Sections (uniform/dense) | Old requested bytes | New requested bytes | Old allocator bytes | New allocator bytes | Savings with overhead |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | 512 / -448 | 1,067 / 423 | 6,152,360 | 1,805,768 | 6,176,216 | 1,836,392 | 70.3% |
| 42 | 0 / -832 | 1,074 / 425 | 6,189,512 | 1,814,392 | 6,213,512 | 1,845,192 | 70.3% |
| 1260202551 | -512 / 960 | 1,253 / 266 | 6,272,072 | 1,164,088 | 6,296,392 | 1,192,664 | 81.1% |

All three checksums match the previous dense representation exactly:
17463511739971487994, 10300230677035997798 and 9084346727055304112,
respectively. Generation averages were 29.943 → 26.752 ms, 27.443 → 26.057 ms and
27.484 → 26.113 ms. These short local timings are indicative, not a frame-rate
claim. Mixed-only data costs slightly more: a dense node and payload request
4,144 bytes, versus the old 4,128; glibc charges 4,176 versus 4,144. An empty or
uniform node is charged 64 bytes.

## Performance and validation

The representation adds a storage-state branch to resident block reads and a
pointer indirection for dense payloads. A first edit to a uniform/empty section
allocates and fills 4,096 bytes; repeated edits to a mixed section reuse its
payload. Detecting restored uniform storage can scan up to 4,096 bytes after a
full-section edit. Terrain generation uses an additional 4 KiB stack staging
buffer and copies mixed payloads. These are explicit tradeoffs for the memory
reduction; the sparse section list and build-local neighbor/column reuse remain.

A serial timing run of the existing mesh-query fixtures (5,000 builds each,
uninstrumented, only the current mesher timed in both source versions) gave:

| Mesh fixture | Old dense storage, µs/build | New storage, µs/build |
| --- | --- | --- |
| Resident surface | 69.305 | 69.182 |
| Synthetic neighbors | 72.549 | 84.599 |
| Synthetic target | 235.429 | 205.095 |
| Edited deep border | 70.113 | 87.850 |
| Sparse high edit | 6.901 | 10.858 |

The timing harness omitted only the timed `baseline_mesh_build` call from
`tests/mesh_query_benchmark.c`; its exact-mesh comparison still ran. These are
single local runs and show noise between runs, particularly for the resident
surface. This change prioritizes resident memory: it does not promise faster
meshing. The fixtures show regressions of about 12 µs with synthetic neighbors,
18 µs on an edited deep border, and 4 µs on a sparse edit, alongside a faster
synthetic-target build.

`mesh_query_profile` still reports seven section lookups per build. Synthetic
neighbor/target sampling remains 64/576 columns in the existing fixture. The
query benchmark compares exact emitted vertices and material ranges with the
pre-005 query implementation, adapted only for section reads and destruction.

`test_section_storage` additionally forces a second equivalent terrain world to
all-dense storage, compares every block and each section's exact mesh, and
exercises every material through uniform/dense/empty transitions, no-op writes,
negative coordinates and partial section bounds. The allocation suite rejects
every expansion allocation for existing uniform/empty sections and new
procedural/air sections; values, ownership, counts, flags and neighboring dirty
state must survive, then a successful retry must work. Materialization rollback
snapshots now copy payload values separately from section metadata.

Performance capture's `resident_voxel_bytes` and streaming capture's owned-byte
total include section nodes plus actual dense payloads. These remain requested
byte metrics; allocator overhead is measured separately by this fixture.

Validation on 26 October 2026: `make test`, `make all streaming_capture
mesh_query_benchmark mesh_query_profile test_renderer`, and
`ASAN_OPTIONS=detect_leaks=0 make test-sanitize` passed. Default LeakSanitizer
cannot finish under this environment's tracing setup, so leak detection was
disabled for the sanitizer run; address and undefined-behavior checks stayed
active. The actual OpenGL renderer suite built but could not start because X11
display `:0` was unavailable; no virtual display executable was installed.
CPU mesh equality and lifecycle tests passed independently.


## Reusable storage follow-up

The historical glibc measurements above describe individually allocated sections.
The current fixture reports requested live bytes and retained pool capacity;
it no longer calls `malloc_usable_size()` on individual section/payload slots.
See [allocation-storage.md](allocation-storage.md) for separate pools, ownership,
exhaustion/reuse tests and current before/after allocation captures.
