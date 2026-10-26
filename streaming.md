# Chunk residency

`renderer_sync_view()` owns streamed gameplay residency and runs before drawing.
It uses the player's eye X/Z, a square radius equal to view distance (maximum
300 world units), and one extra chunk on each side. Bounds are clipped to the
world. This is deliberately independent of camera orientation and vertical
frustum culling. Non-streamed fixture worlds use `renderer_sync_world()`.

The baseline is the world bounds, transform and seed. Reads of missing terrain
synthesize that baseline without allocating voxels. `world_materialize_chunk()`
transactionally adds dense CPU sections. Changes are authoritative CPU block
values; meshes are derived data. A changed procedural chunk is marked edited,
even if a later write restores the baseline.

On movement or view-distance reduction, synchronization first deletes departing
VAOs/VBOs and marks retained sections dirty for rebuilding on return. It then
releases departing unedited CPU chunks using a conservative materialized chunk
envelope maintained by the world. It visits only departing strips; an envelope
already inside the view requires no scan. Successful materialization expands the
envelope even when renderer setup subsequently fails. Edited chunks outside the
envelope remain protected by their edit flag. CPU eviction never
relies on the existence of a GPU mesh. Current mesh records and visible indices
are rebuilt before drawing. Failed metadata allocation leaves the old record
array owned by the renderer, but departing handles have already retired and
visible indices have been cleared. Missing resources can be retried next frame.
Meshing/upload is synchronous: no worker queue or pending CPU mesh survives the
call. Failed uploads retain the old mesh and dirty flag; leaving retires that
old mesh and returning builds from current CPU values.

Edited CPU chunks stay resident until world destruction to preserve all edits,
including removals and writes outside the generated vertical range. Their GPU
meshes retire normally. There is no save/load support. There is no fixed RAM cap
for edited terrain: each edited chunk can retain its entire generated column
plus arbitrary edited sections, and each retained section costs `sizeof(Subchunk)` plus 4,096 bytes when dense.
Empty and uniform sections have no block payload; see [storage measurements](section-storage.md).
Empty edit markers and renderer metadata also remain. Persistent edit storage
is tracked in [issue 011](issues/011-persistent-edit-storage.md).

For unedited terrain, let `D` be the radius and `S = 16 * blockSize` the chunk
span. At most `(ceil(2*D/S)+3)^2` chunks reside, each with at most 32 generated
sections. CPU section storage is bounded by that count times `(sizeof(Subchunk) + SECTION_VOLUME)`, before allocator overhead.
At block size 1 the conservative chunk limits are 49 at radius 25, 256 at radius
100, and 1681 at radius 300. Actual counts depend on alignment, terrain height
and world edges. GPU records follow resident sections; only sections with
geometry own GL objects. Chunk metadata for the whole bounded world is allocated
at world creation. Visible/draw index arrays retain their maximum capacity.
Allocator and driver caches can keep process RSS above currently owned storage.

# Reproducing travel measurements

```
make streaming_capture
./streaming_capture > /tmp/bwildering-streaming.csv
```

Requires a working X11 display and OpenGL 3.3 context. The benchmark uses seed
1260202551, side 4608, block size 1, radius 25, no edits, an invisible 640×480
window and swap interval zero. It visits 32 positions 96 units apart from
X=-1487.5 to X=1488.5, returns through those positions, then leaves the world.
Each stop asserts the residency bound and the final stop asserts zero resident
CPU sections and GPU geometry. This deliberately stresses cold movement; it is
not a normal walking frame-rate measurement and does not draw or swap frames.

CSV rows report sync/materialization/meshing/upload CPU submission times, a
separate `glFinish` drain time, resident counts, owned world/renderer storage,
Linux process peak RSS (`ru_maxrss`, KiB), requested GPU vertex bytes and
retirement counts/bytes. Owned storage excludes temporary mesh allocations,
allocator overhead, GL driver storage and unrelated application state. Requested
GPU bytes are vertex payload sizes, not measured physical VRAM consumption.
Retirement records GL deletion requests; the driver determines physical reuse.
Peak RSS includes library/driver memory and is a high-water mark.

Captured 26 October 2026, GCC `-O2 -std=c99`, AMD Radeon HD 7800 Series
(radeonsi, pitcairn, ACO, DRM 3.64, 7.3.0-4-cachyos):

| Measurement | Result |
| --- | ---: |
| Peak resident chunks / conservative limit | 36 / 49 |
| Peak resident sections | 885 |
| Peak owned world/renderer CPU bytes | 5,746,796 |
| Peak requested GPU vertex bytes | 4,229,424 |
| Peak process RSS | 82,680 KiB |
| CPU chunks evicted | 2,268 |
| GPU meshes retired | 6,965 |
| Mean / maximum materialization | 8.542 / 31.529 ms |
| Mean / maximum meshing | 65.183 / 171.789 ms |
| Mean / maximum upload submission | 3.403 / 8.578 ms |
| Mean / maximum sync submission | 77.957 / 195.414 ms |
| Final CPU chunks / sections / GPU vertex bytes | 0 / 0 / 0 |

Unedited residency stays bounded across 6,000 units of outward/return travel;
synchronous cold generation and meshing still cause visible frame-time spikes.
The benchmark exposes that cost rather than imposing a timing threshold.

Validation: `make test` and `make test-renderer` passed. The renderer suite
covers regeneration, edited revisits, radius shrink, CPU materialization followed
by metadata allocation failure, and leaving after a failed GPU upload. The last
case verifies GL names are deleted, edits survive, and regeneration matches every
baseline voxel in an unedited chunk. ASan/UBSan passed with
`ASAN_OPTIONS=detect_leaks=0 make test-sanitize`; LeakSanitizer cannot run under
this environment's tracing.

# Frame-path regression check

```
make streaming_capture
./streaming_capture --sync
```

This mode disables instrumentation, warms the radius-100 neighbourhood, then
reports five samples of 1,000 stationary sync calls and 120 walking calls at
one world unit per call. Timed loops exclude drawing and GPU drains. The world,
seed and GL device match the travel benchmark. This measures synchronization
cost rather than complete frame time.

The first issue-001 implementation introduced a full-world CPU eviction scan
on every frame: 82,944 chunk slots at the default side length, including release
calls for absent columns. A repeated comparison of identical sync loops measured:

| Version | Median stationary sync, ms/call |
| --- | ---: |
| Before issue 001 | 0.139 |
| First issue-001 implementation | 0.623 |
| Materialized-envelope fix | 0.129 |

A separate final `--sync` run measured a 0.140 ms stationary median and
2.224 ms average walking sync. Walking includes synchronous materialization and
meshing when crossing residency boundaries; these spikes remain. Timings vary
with machine load and are not test thresholds. The fixed travel capture still
peaked at 36 chunks and 885 sections, retired 6,965 meshes, and ended with zero
resident baseline chunks, sections and GPU bytes. Its mean/maximum sync times
were 77.704/201.592 ms, consistent with cold generation dominating that scenario.
