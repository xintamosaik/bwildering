# Allocation accounting and reusable storage (27 October 2026)

Allocation churn is now separated from retained capacity. Section nodes and dense
payloads use independent world-owned page pools, immediate GPU uploads borrow
renderer-owned mesh scratch, and renderer identity merges reuse two arrays.
The public `section_mesh_build()` still returns independently owned vertices.

## Capacity and ownership contracts

- A node page contains 128 nodes (8,200 requested bytes on this build); a payload
  page contains 64 independent 4 KiB slots (262,152 bytes). Pages are allocated
  lazily, reuse freed slots first, and remain owned until world destruction.
  These page sizes amortize the measured thousands of small requests while
  limiting last-page slack to 127 nodes and 63 payloads. Initializing a page
  links its slots; payload bytes are initialized only when acquired. Every node
  acquisition resets all fields, and every payload acquisition initializes all
  4,096 bytes before publication. Worlds never share storage.
- The library default permits `chunkCount * 32 + 65536 + 32` live slots in each
  pool: every generated section, one sparse cache section per possible edit,
  and one replacement column coexisting with its old data. This supports full
  terrain generators as well as arbitrary library residency. This limit does
  **not** reserve that many bytes. `world_storage_limits()` can override limits
  before any pages are committed, including zero for exhaustion fixtures.
- Gameplay configures a tighter bound from the maximum supported view radius:
  `min(chunksPerSide, ceil(2 * 300 / (16 * blockSize)) + 3)` columns per axis,
  times 32 sections, plus 65,536 overlay sections and 32 staging slots. At block
  size 1 in the default world this is 119,360 slots per pool. The conservative
  all-dense ceiling is about 489 MB of payloads; it is a limit, not a startup
  reservation or a measurement of typical demand. Sparse signed section links
  and the existing edit arena remain unchanged.
- Pool exhaustion or failed page allocation returns failure before publishing
  the section/edit. Transactional materialization returns staged nodes/payloads
  to their pools. Failed work can increase retained capacity, but preserves all
  authoritative voxel data, links, revisions, dirty flags and existing ownership.
  Hard exhaustion needs eviction/free capacity; transient failure can be retried.
  Streaming tasks retain signed identities/revisions, never pooled pointers.
- Mesh scratch is one renderer-owned allocation, committed on first build:
  `16^3 * 6 faces * 2 triangles * 3 vertices * sizeof(SurfaceVertex)` = 1,769,472
  bytes. This conservative bound covers every current opaque face rule; the
  checkerboard fixture exercises 73,728 vertices. The builder checks vertex/byte
  arithmetic and caller capacity before writing. `section_mesh_build_scratch()`
  leaves output unchanged on failure and returns borrowed vertices; never pass
  that output to `section_mesh_destroy()`. The renderer releases its scratch
  scope only after `glBufferData()` has consumed the bytes, including failure.
  No mesh storage crosses frames or is shared between renderers.
- Renderer metadata has an active buffer and a spare staging buffer. Capacity
  doubles from 64 records, with arithmetic guards. Reserve/grow staging before
  mutating active records, merge stable identities, retire departures, transfer
  handles, clear the old records, then retain the old buffer as staging. Only
  active records own GL handles. During growth, active, old staging and new
  staging can coexist; accounting includes all three. Visible/draw index arrays
  also grow geometrically and retain capacity across radius shrink/departure.
  Failed index growth keeps prior storage and uses the existing submission
  fallback. Destruction frees both metadata buffers, scratch and index arrays.

## Accounting and reproducible captures

`storage_accounting(1)` enables single-threaded diagnostic counters. Enable it
before creating owners, destroy those owners before disabling/resetting it.
Normal allocations do not read clocks. Categories are world metadata/overlay,
section nodes, dense payloads, CPU mesh storage, renderer records, visible indices
and draw indices. The diagnostic records logical reservation attempts (including
reuse), cumulative requested bytes, current/peak live reservations, current/peak
backing capacity, backing malloc calls, reused reservations and failures. The
optional reservation rejection hook sees reused slots too; it replaces linker
malloc wrappers in failure tests. Backing allocation failures remain retryable.

Live reservation bytes and committed capacity are deliberately different. A
scratch request reserves its entire safe bound, so cumulative mesh requested
bytes can exceed owning-path requests despite reducing backing malloc calls to
one. Page capacity includes page headers; category peaks include transactional
overlap and are cumulative since initialization. They are separate category
maxima, **not** a simultaneous whole-process peak. Capture `waste` is capacity
minus live reservation bytes, except index arrays use their current populated
counts. Free scratch capacity is therefore entirely available/waste between
builds. Allocator headers, fragmentation, uncommitted virtual memory, driver
allocations and RSS are not represented as owned capacity. RSS peak and requested
GPU vertex bytes have their own columns; GPU driver overhead is unknown.

Run `make allocation_capture && ./allocation_capture` with access to local X11.
The invisible-window capture performs 32 synchronous sync/index-collection
samples per scenario: convergence, stationary, cold travel, one-block movement,
repeated edits, sparse overlay growth, maximum radius, reservation failure/retry
and a repeated route. GPU drains occur outside the sample loops. Timings exclude
individual edit preparation and measure synchronization/index collection, not a
whole rendered frame. Initialization time includes chunk metadata and shader/text
resources, with lazy pool/scratch cost included in first convergence. This is a
storage stress capture; normal scheduled-frame evidence remains in streaming.md.

Raw [baseline](plans/review-follow-up/allocation-measurements/baseline.csv) and
[reusable](plans/review-follow-up/allocation-measurements/reusable.csv) captures
use GCC 16.2.1, `-Wall -Wextra -O2 -std=c99`, seed 1260202551, side 4608,
block size 1, Intel Core i5-6500, accelerated AMD Radeon HD 7800 Series,
Mesa 26.2.5, X11 `:0`, OpenGL 3.3 requested (4.6 supplied). Comparisons run
serially with allocation accounting enabled. The baseline is commit
`a0993004f75074117d94e1911af2b66f4656e62d` with only diagnostic allocation
wrappers, preserved in [this patch](plans/review-follow-up/allocation-measurements/baseline-instrumentation.patch).
To reproduce it, unpack that commit into a temporary directory, apply the patch,
copy current storage.c/storage.h and tests/allocation_capture.c into it, and
compile the capture with `-DSTORAGE_BASELINE`, game_logic.c, voxel_world.c,
storage.c, performance.c, ui.c and input.c and the usual GL libraries. Baseline
draw-index diagnostics use allocate/copy/free instead of realloc to expose
replacement overlap; an actual realloc may grow in place. No legacy production
storage path is retained.

## Evidence and limits

The final serial capture measured the following backing allocation calls and
sync/index-collection times (milliseconds). Each timing cell is p50 / p95 / max;
with 32 samples p99 is also the maximum. These are single-run distributions,
not confidence intervals.

| Scenario | Calls before → after | Time before | Time after |
| --- | ---: | ---: | ---: |
| converge | 335 → 7 | 0.113 / 0.228 / 16.129 | 0.193 / 0.291 / 18.466 |
| stationary | 0 → 0 | 0.107 / 0.150 / 0.157 | 0.120 / 0.190 / 0.211 |
| cold | 22,308 → 14 | 19.531 / 33.086 / 33.914 | 19.494 / 33.702 / 34.371 |
| movement | 157 → 0 | 0.114 / 2.473 / 2.756 | 0.111 / 2.455 / 2.484 |
| edits | 37 → 2 | 0.124 / 0.172 / 0.204 | 0.123 / 0.140 / 0.143 |
| overlay | 76 → 0 | 0.115 / 0.176 / 0.186 | 0.116 / 0.190 / 0.197 |
| maximum | 23,264 → 180 | 0.945 / 1.393 / 762.995 | 0.695 / 0.960 / 727.821 |
| failure_retry | 33 → 0 | 0.236 / 0.260 / 5.547 | 0.232 / 0.265 / 5.346 |
| repeat | 22,494 → 0 | 25.234 / 34.592 / 35.560 | 23.953 / 32.998 / 33.838 |

Initialization was 6.144 → 6.829 ms; the first convergence maximum was
16.129 → 18.466 ms, including lazy storage commitment. Cold-travel timing is
similar despite eliminating most backing requests; terrain and upload work still
dominate. Repeated-route median was 25.234 → 23.953 ms. These measurements support
removing churn, not a broad claim of faster frames. Stationary median increased
from 0.107 to 0.120 ms in this sample; no stationary allocations occur in either
version.

At maximum radius, requested retained capacity was 25,394,748 → 27,588,672
bytes, with live reservations 25,394,748 → 25,525,900 bytes. After the repeated
small-radius route it was 4,703,184 → 27,588,672 retained bytes, versus
4,703,184 → 4,717,320 live reservation bytes. The increased retained memory is
intentional reuse capacity. Per-category peaks and cumulative requests, including
old/new metadata overlap, are in the raw files. All 32 failure/retry samples
record a rejected mesh reservation and then successful convergence.

The stationary capture asserts zero reservation attempts and zero backing
allocations. A warmed route also performs zero backing allocations in the new GL
fixture. See the raw captures for per-category requested/live/peak capacity,
reuse, allocation counts, initialization cost and timing distributions.

The largest tradeoff is retention: after maximum-radius residency, subsequent
small-radius travel reuses pages rather than returning them to malloc. This
reduces churn while retaining the measured high-water capacity until destruction.
No automatic trimming is implemented; a trim policy would need measurements
that justify its added lifetime and failure cases. Timing improvements are not
a frame-time guarantee: terrain sampling, mesh construction, driver uploads,
metadata merging and full-grid scans still take time.

`make test`, `make all`, real-context renderer tests, ASan/UBSan shared tests
(`ASAN_OPTIONS=detect_leaks=0`) and Clang-Tidy's
`bugprone-narrowing-conversions` check cover the migration. LeakSanitizer is
excluded under tracing; the sanitizer result does not certify process leaks.
Accounting fixtures independently verify zero owned live/capacity bytes after
destruction. New tests exercise node/payload exhaustion, partial-column rollback,
reuse after eviction/compaction, cross-world isolation, scratch overflow and exact
mesh/material equality, warm renderer routes, scratch failure retaining old GL
geometry, retry and stationary convergence. Existing exhaustive reservation
failure tests and GL depth/range/handle-ownership fixtures remain active.

The owning public mesh API intentionally still allocates for callers that need
independent lifetimes. Chunk metadata and the edit overlay already have bounded
world lifetimes, so they remain separate arena allocations. GPU allocations and
index/record compaction are deferred; none is claimed to be pooled here.
