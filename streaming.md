# Chunk residency

`renderer_step_view()` owns streamed gameplay residency and runs before drawing.
`renderer_sync_view()` retains a synchronous drain for startup and fixtures.
It uses the player's eye X/Z, a square radius equal to view distance (maximum
300 world units), and one extra chunk on each side. Bounds are clipped to the
world. This is deliberately independent of camera orientation and vertical
frustum culling. Non-streamed fixture worlds use `renderer_sync_world()`.

The baseline is the world bounds, transform and seed. Reads of missing terrain
synthesize that baseline without allocating voxels. `world_materialize_chunk()`
transactionally stages compressed CPU sections. Resident sections take precedence;
missing data reads the session overlay, then the procedural baseline. Collision,
picking, surface heights, temporary mesh sections and mesh boundary reads share
this contract. Reads never allocate resident terrain just to find an edit.

A procedural write compares against the seed rather than the cached value. Each
retained difference is a signed voxel identity and replacement material; explicit
air removes terrain. Restoring the baseline releases the entry, and identical
writes leave counts, ownership and dirty flags unchanged. Outside [-128,384),
the baseline is air. Nonprocedural fixture worlds retain their existing resident
storage semantics.

The first changed procedural write reserves a single world-owned arena with
**65,536 entries**. Each chunk has a one-based sparse list index; released slots
form a world free list. On this 64-bit build an entry is 20 bytes, so the arena
owns **1,310,720 bytes (1.25 MiB)**; each chunk adds a four-byte head index,
increasing the aligned `Chunk` size from 24 to 32 bytes on this build. It allocates no individual edit nodes. The arena
stays reserved until world destruction; logical occupancy follows actual
differences, and restoration makes slots reusable. Lists cost O(edits in the
chunk) to query. This is a bounded initial implementation; heavy concentration
in one chunk can make reads and staging expensive.

The capacity fixture in `test_game_logic` travels through 1,024 columns, writing
64 edits per column at Y=500 across every solid material, and releases each CPU
cache before moving. It fills the 65,536-entry pool with only one resident section
at a time. It then revisits every column twice, with at most 33 resident sections,
and ends with zero resident sections/voxels and 65,536 retained edits. This gives
room for 64 changes in each of 1,024 visited columns within 1.25 MiB. It is a
maximum retained-difference count, not unlimited editing. A new difference at
capacity returns zero from `world_set_block`; updates to existing entries,
identical writes and restorations still succeed subject to resident allocation.
The fixture checks rejection and reuse after restoration.

Mutation reserves overlay and writable section capacity before publishing either.
Allocation failure preserves counts, links, revisions, dirty flags and overlay
ownership. Reconstruction stages all missing baseline and overlay sections,
including separated high/low edits and empty sections, before publication.
Resident counters exclude unloaded overlay entries. Edits and their neighbors
conservatively participate in meshing, including buried sections exposed by
removals.

On movement or view-distance reduction, synchronization deletes departing
VAOs/VBOs, discards scheduled identities outside the new bounds, then releases
all departing procedural CPU columns, including edited ones. A conservative
resident envelope includes partially cached edit columns and visits only
departing strips; unchanged bounds require no scan. CPU eviction is independent
of successful renderer metadata allocation. Scheduled tasks own coordinates and
revisions rather than section pointers. Returning reconstructs seed plus overlay;
restored-baseline empty retirement markers need not survive CPU eviction.
The renderer tests travel through 60 edited locations with a 49-column residency
limit and compare the original edited view on return. They also check GL deletion
and exact rebuilt vertex bytes after leaving with a failed upload.

Validation on 27 October 2026: `make all`, `make test`, the real-display
`test_renderer` suite, and ASan/UBSan suites passed (leak detection disabled under
tracing). Clang-Tidy `bugprone-narrowing-conversions` checked all changed C files;
existing warnings remain in unchanged code, with no introduced warnings.

These edits are session-local and are lost on world destruction. There is no
save/load or spill-to-disk support. Disk persistence remains a separate phase:
see [issue 011](issues/011-persistent-edit-storage.md).

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


# Scheduled frame work (27 October 2026)

Gameplay collects residency separately from execution. A renderer-owned array
holds at most 1,681 tasks (`41*41`, the conservative maximum column count at
radius 300 and block size 1). Tasks store signed chunk/section coordinates,
section revision and admission frame; they never borrow section pointers or
record indices. Chunk and section work share this capacity. Overflow leaves
missing columns absent and sections dirty, and rotates the next admission scan.
Smaller block sizes and arbitrary edited sections use the same bounded queue.
The total discovered work count can exceed queue capacity; only queue storage
is capped. Edited columns use the same CPU residency policy as baseline terrain.

Collection discards tasks outside the desired bounds or already completed and
refreshes changed revisions. Execution validates section identity/revision again
and resolves the current GPU record. Priority combines horizontal distance,
vertical distance for sections, a preference for edited columns, and age. Each
waiting frame subtracts half a chunk from the priority score, eventually
overcoming both distance and the 64-chunk edit preference. Signed identity breaks
ties deterministically. Rotating admission prevents a full queue from losing
unadmitted dirty work. Newly materialized sections join a later frame's collection.

The selected new-work budget is **2 ms, 128 operations, and 2 MiB of requested
section upload bytes per frame**, whichever is reached first. Checks occur before
starting another operation. One operation is either a transactional chunk
materialization or a section build plus upload and temporary mesh destruction.
A section can cross the byte threshold; a single operation can cross the time
threshold. Retirement, queue collection/sorting and renderer metadata remain
outside this new-work budget and are measured separately. These are soft limits;
they provide no hard frame-time guarantee. The operation cap was raised from an
initial 16 after captures showed excessive large-radius convergence latency.

Old successful meshes remain drawable while replacements wait or fail; entirely
new sections appear only after successful upload. CPU/procedural reads, edits and
collision remain authoritative throughout loading. Metadata allocation failure
uses existing index storage to draw surviving old meshes. Sync reports complete,
deferred or failed separately; queue pressure is deferred progress. Gameplay
retries failures on subsequent frames, retaining dirty work and old geometry.
The synchronous path returns success/failure and drains all desired work for
startup, captures and existing fixtures.

## Reproducing the comparison

```
make streaming_capture
./streaming_capture --baseline > /tmp/streaming-baseline.csv
./streaming_capture --frames > /tmp/streaming-scheduled.csv
./streaming_capture > /tmp/streaming-cold.csv
```

Run these serially on the same GL device. Baseline uses the synchronous reference;
frames uses the production budget. Both enable instrumentation and run seven
240-frame scenarios at each radius 25, 100 and 300, with fixed 1/60 s simulation
steps and real `update_player()` walking/running speeds (4/6 world units/s,
subject to terrain collision). Each radius begins at negative X/Z (-16.5,-16.5)
with synchronously warmed residency. Scenarios are stationary, walking, running,
teleport to mountainous terrain (-1200.5,-900.5), reverse direction, radius
25 then restoration at frame 120, and a world-edge teleport (2300.5,-2300.5).
Simulation follows the same inputs in both modes; collision reads procedural
terrain even when geometry has not loaded. Mountain collision can stop travel.

Full-frame samples include fixed simulation, sync, clear/render and swap CPU
submission, excluding reporting/snapshot scans and event polling. The invisible
window is 640x480, depth/culling enabled, swap interval zero. There is no
`glFinish` in a frame loop. Explicit GPU drains before/after scenarios are outside
the distributions; upload times describe CPU submission, not GPU execution.
Convergence frames count additional sync-only budget passes after the 240 sampled
frames, excluding the final empty confirmation pass. Atomic maximum also covers
those tail passes. Other work totals and residency peaks cover the sampled window.
CSV includes p50/p95/p99/max sync, CPU render submission and full-frame times, disjoint materialization,
meshing, upload submission, retirement and metadata times, operation/build counts,
bytes, residency, discovered pending work, queue occupancy and owned CPU storage.
The original 64-stop radius-25 cold-travel stress remains separate.
The render submission columns were added during the
[renderer audit](renderer-audit.md); its linked captures include those columns
and the full compiler/vendor/version banner. Earlier captures below predate them.

## Captured results

GCC `-Wall -Wextra -O2 -std=c99`, seed 1260202551, side 4608,
block size 1, AMD Radeon HD 7800 Series (radeonsi, pitcairn, ACO,
DRM 3.64, 7.3.0-4-cachyos). These are instrumented serial single runs,
not machine-independent thresholds. Raw captures retain every percentile,
work category, byte count and residency measurement:
[synchronous reference](plans/review-follow-up/streaming-measurements/baseline.csv),
[scheduled frames](plans/review-follow-up/streaming-measurements/scheduled.csv),
[cold travel](plans/review-follow-up/streaming-measurements/cold-travel.csv).

Normal movement sync distributions in milliseconds (p50 / p95 / p99 / max):

| Radius | Scenario | Synchronous | Scheduled |
| --- | --- | --- | --- |
| 25 | stationary | 0.089 / 0.125 / 0.153 / 0.175 | 0.079 / 0.112 / 0.164 / 0.223 |
| 25 | walking | 0.090 / 0.126 / 0.158 / 2.152 | 0.079 / 0.114 / 0.162 / 1.466 |
| 25 | running | 0.099 / 0.135 / 0.205 / 0.367 | 0.079 / 0.109 / 0.161 / 0.186 |
| 100 | stationary | 0.181 / 0.228 / 0.289 / 0.312 | 0.104 / 0.151 / 0.207 / 0.233 |
| 100 | walking | 0.182 / 0.237 / 0.327 / 7.683 | 0.103 / 0.160 / 2.134 / 2.306 |
| 100 | running | 0.177 / 0.232 / 0.254 / 0.272 | 0.104 / 0.149 / 0.178 / 0.259 |
| 300 | stationary | 1.148 / 1.462 / 1.591 / 1.597 | 0.496 / 0.736 / 0.894 / 0.966 |
| 300 | walking | 1.199 / 1.534 / 1.801 / 20.621 | 0.560 / 1.027 / 3.791 / 4.105 |
| 300 | running | 1.233 / 1.633 / 1.820 / 24.770 | 0.523 / 0.927 / 3.445 / 5.153 |

Cold-transition results (milliseconds). Loading frames run from the last request
until the first complete response, including the empty confirmation pass. Radius
rows measure the restoration request at frame 120. Synchronous loading takes one
frame; scheduled loading spreads that work across the reported frames.

| Radius | Scenario | Sync max, baseline → scheduled | Full frame p99, baseline → scheduled | Full frame max, baseline → scheduled | Loading frames, scheduled |
| --- | --- | --- | --- | --- | --- |
| 25 | teleport | 29.904 → 2.700 | 0.488 → 2.661 | 30.220 → 2.874 | 15 |
| 25 | radius | 0.268 → 0.421 | 0.374 → 0.553 | 0.705 → 0.573 | 1 |
| 25 | edge | 48.238 → 2.525 | 1.100 → 2.521 | 48.863 → 2.802 | 9 |
| 100 | teleport | 165.845 → 4.448 | 0.806 → 4.201 | 166.228 → 5.034 | 80 |
| 100 | radius | 115.339 → 3.072 | 0.884 → 3.162 | 115.757 → 3.312 | 60 |
| 100 | edge | 63.555 → 7.184 | 0.923 → 3.452 | 63.958 → 7.582 | 49 |
| 300 | teleport | 932.790 → 8.153 | 3.988 → 5.022 | 933.654 → 8.521 | 456 |
| 300 | radius | 899.627 → 8.093 | 5.241 → 3.943 | 900.789 → 8.449 | 445 |
| 300 | edge | 332.155 → 10.128 | 1.378 → 3.456 | 332.718 → 10.597 | 161 |

The largest sampled full frame fell from **933.654 to 10.597 ms**. The worst
measured atomic operation across the final pair was **2.315 ms** (scheduled:
1.988 ms). Exploratory runs observed occasional roughly 9 ms atomic outliers;
allocation, driver and OS delays vary between runs. Transactional atomic work
can still overrun the 2 ms target. Incremental staging is a follow-up if those
outliers are unacceptable, with transactionality preserved explicitly.

The scheduled queue peaked at its fixed **1,681 slots**, while discovered pending
work peaked much higher (see CSV). Overflow converged without dropping dirty
state. Radius-300 teleport required **456 frames**, and restoration **445**:
about 7.6/7.4 seconds at a hypothetical steady 60 Hz. Loading latency is a visible
tradeoff. Nearby sections appear first, and distant geometry fills in gradually;
there is no loading screen or collision dependence on mesh readiness. Tail passes
are sync-only, so these frame counts are more useful than their capture wall time.

Retirement remains a separate source of spikes on teleports: deleting the previous
large neighborhood is deliberately immediate. Queue scans, sorting, section
lookup, record rebuilding and index growth also add unbudgeted CPU time. The
largest final sync spike was 10.128 ms despite a 2 ms new-work target. Warm
radius-300 metadata is still measurable; budgeting new work does not eliminate
that cost. Mountain collision stopped the reverse route in this run, so its
zero-work frames do not establish a cold reverse-travel bound.

The separate cold-travel drain still peaked at 36 chunks / 885 sections and
4,229,424 requested GPU vertex bytes, retired 6,965 meshes, evicted 2,268 chunks,
and ended with zero CPU sections and GPU bytes. Its synchronous average/max sync
was 19.802/37.419 ms on this run. Historical measurements above used earlier
meshing implementations and are retained as dated evidence, rather than used as
the scheduling baseline.

Validation: `make all`, `make test`, `ASAN_OPTIONS=detect_leaks=0 make test-sanitize`
and the real GL renderer/application lifecycle suite passed. The renderer suite
adds deterministic operation/byte-budget, nearest-work, aging, exhaustion/rescan,
stationary no-work, revision-refresh, edit/revisit/shrink/cancellation,
metadata/index allocation failure and upload retry cases. Converged material
ranges and actual driver-owned vertex bytes match synchronous section builds.
Clang-Tidy `bugprone-narrowing-conversions` ran on every changed C file with the
build flags; existing narrowing warnings remain in unchanged lines, with none
introduced. LeakSanitizer remains excluded under tracing; this is not a leak check.
