# Renderer verification — 27 October 2026

The full renderer, including `main.c`, passed the real-context ownership,
submission, depth and staged-streaming checks in `make test-renderer`.
This closes the original review's missing-renderer verification gap for this
implementation and device. Responsiveness remains a separate concern: the
scheduler has soft budgets, and cold loading and metadata/retirement work can
still cause spikes. See [streaming policy and scheduling](streaming.md) and
[issue 010](issues/010-renderer-lifecycle-audit.md).

## Reproduction and device

Run from the repository root, serially, on an accessible X11 display:

```sh
make all
make test
make test-renderer
ASAN_OPTIONS=detect_leaks=0 make test-sanitize
clang-tidy tests/test_renderer.c tests/streaming_capture.c \
  -checks='-*,bugprone-narrowing-conversions' -- -std=c99 -Wall -Wextra -O2
make streaming_capture
./streaming_capture --baseline > /tmp/renderer-baseline.csv
./streaming_capture --frames > /tmp/renderer-scheduled.csv
```

- GCC 16.2.1 20260810, `-Wall -Wextra -O2 -std=c99`; Clang-Tidy 23.1.3.
- Intel Core i5-6500 @ 3.20 GHz, four logical CPUs; Linux 7.3.0-4-cachyos.
- GL vendor AMD; renderer AMD Radeon HD 7800 Series (radeonsi, pitcairn, ACO,
  DRM 3.64, 7.3.0-4-cachyos), 2 GiB dedicated VRAM.
- OpenGL 4.6 Core Profile, Mesa 26.2.5-arch3.1; GLFW requests a 3.3 core
  context. Local accelerated X11 display `:0`, invisible 640×480 window.
  Depth-reference captures use a 320×240 viewport in that window.
- Seed 1260202551, streamed world side 4608, block size 1 for timing captures;
  swap interval zero, depth testing and face culling enabled, overlay disabled.

The sandbox could not open `:0`; that run failed at `glfwInit()` and is not
counted as a pass. Running with access to the existing local display succeeded.
No virtual display or software renderer was substituted. Build, logic,
allocation-failure, menu, real-GL and ASan/UBSan checks passed. LeakSanitizer was
explicitly disabled for the sanitizer run under tracing; leaks are not certified
by that run. Both changed C files passed the narrowing-conversion check.

## Ownership audit

| Operation | Before / success | Failure / departure |
| --- | --- | --- |
| `renderer_init` | Renderer owns shader program and text VAO/VBO. Shader objects are deleted after linking. | Partial initialization calls `renderer_destroy`; no world ownership is acquired. |
| `renderer_prepare_world` | Renderer owns the record array. A sorted identity merge copies matching VAO/VBO handles into replacement records, deletes departing objects, frees the old array without destroying transferred handles, then commits the array. | Allocation happens before mutation; failure retains the old array and live handles. Successful merge leaves exactly one record owning each nonzero handle. |
| `renderer_sync_section` | Borrows a world section by signed identity; owns a temporary CPU `SectionMesh` only during the call. Successful upload clears dirty and always destroys the temporary CPU mesh. | Build/upload failure destroys temporary storage, retains old drawable GPU data and leaves dirty set. Missing sections destroy their GPU record. Empty meshes retire old objects and retain zero-handle identity markers. |
| `renderer_upload_section_mesh` | A temporary result owns new VAO/VBO handles until upload succeeds. Commit destroys the target's old GL objects and transfers the result to that target. | Invalid ranges or GL errors destroy only temporary objects; the target retains handles, vertex bytes, bounds and material ranges. |
| `renderer_sync_view_work` / synchronous wrapper | Renderer owns index arrays and the fixed-capacity task queue. Index entries borrow current record indices; tasks hold coordinates/revisions, never section pointers or record indices. | Departure deletes GPU objects before releasing procedural CPU columns and cancels departing tasks. Retirement is allowed to commit before a later failure. New-work failures retain old in-range geometry; scheduled metadata failures rebuild a valid fallback index list. |
| Visible/draw list growth | New storage commits after allocation. Lists are rebuilt after metadata replacement; final draw lists are camera-specific. | Failed visible growth keeps owned storage; the synchronous pass clears counts. The scheduled pass reconstructs surviving indices. Failed final draw-list growth discards partial counts and draws through direct visibility checks. |
| `world_destroy` / `renderer_destroy` | World owns CPU sections, chunk metadata and edit-overlay storage. Renderer independently owns GL objects, record/index arrays and its inline queue. | World destruction leaves GL objects alive. Renderer destruction deletes section/text/program resources, releases arrays and zeroes state while the context is current. A current program is deletion-pending until unbound; the test unbinds it before asserting `glIsProgram == false`. Repeated renderer destruction is safe. |

Synchronization is transactional per materialization or section replacement,
not per entire view. Earlier successful replacements and departures can commit
before later failures. The application shuts down CPU world storage, then the
renderer, then its GLFW window/context. No renderer record owns a CPU section.

## Regression evidence

Existing fixtures already covered metadata and visible-index allocation failure,
final draw-list fallback, failed replacement followed by departure, empty markers,
negative section Y, repeated exploration, radius 25→300→25, edited revisits and
scheduler exhaustion/aging/revision refresh. These tests remain in place.

`test_unculled_transforms_seams_and_destruction` adds an independent drawing loop
that submits **all** owned section/material ranges without consulting streaming,
distance or frustum predicates. It compares against production rendering with
identical projection/view transforms and hardware clipping. The fixtures cover:

- Negative X/Y/Z section identities; origins (0,0,0) and (-57.25,31.5,-83),
  block sizes 1 and 2.5; edits across X/Y/Z section seams and two materials.
- Near-plane intersections, both side-plane crossings, geometry behind the
  camera, far-plane crossings at ordinary distances and maximum distance 300.
- Twelve repeated edit/upload cycles with CPU-to-GPU vertex-byte and exact
  material-range comparisons, plus metadata insertion before live identities.
- Unique nonzero VAO/VBO ownership after synchronization and metadata transfer;
  all recorded GL names deleted after destruction, including text/program objects.

Depth must be finite; coverage masks must match exactly and individual depth
values differ by at most 0.00001. There are no screenshot goldens. During each
production/reference capture, the draw wrapper checks that each submitted VAO
and its attribute buffer are live and belong to one current record, and that
the range matches one material. Optimized submissions must be unique members of
the unculled reference; hardware `GL_PRIMITIVES_GENERATED` must equal the sum
of submitted triangles. Frustum culling can legitimately omit offscreen ranges,
so total submitted primitive counts need not equal the reference's count.
The compact fixtures isolate clipping boundaries from the renderer's separate
coarse spherical view-distance policy; the existing AABB-distance fixture also
checks exact inclusion and exclusion at radius 25.

`test_rendered_pending_edits_and_cancellation` checks real draws and unchanged
depth/old GPU bytes while an authoritative removal waits under a zero-operation
budget. Fast movement outside the world cancels pending work and deletes the
old objects before any allocation can reuse their names. Rendering then submits
nothing. Returning with a larger radius and a deterministic operation budget
converges to a separately created world drained synchronously: driver-owned
vertex data, every material range and rendered depth agree. The established
loading policy remains: old successful meshes stay visible, entirely new
sections are absent until uploaded, CPU reads and collision are current.

## Responsiveness measurements

Timing results are recorded separately from correctness assertions. The raw
[baseline capture](plans/review-follow-up/renderer-measurements/baseline.csv) and
[scheduled capture](plans/review-follow-up/renderer-measurements/scheduled.csv)
contain stationary, walking, running, teleport, reverse, radius-change and
world-edge scenarios, each with 240 fixed 1/60-second steps at radii 25/100/300.
Routes and movement speeds are defined in `tests/streaming_capture.c` and
[streaming.md](streaming.md#reproducing-the-comparison).

Captures enable performance instrumentation. Synchronization is measured around
the sync call, submission around `render_world` alone, and whole-frame time
around simulation, sync, clear, render and swap. Submission is CPU/driver time,
not GPU execution. No explicit GPU drain occurs inside the measured frame loop;
pre/post-scenario drains and additional convergence passes stay outside it.
Raw samples report p50/p95/p99/max for all three categories. Desktop activity and
driver stalls can affect single runs; no hardware-specific timing assertions
are imposed. Scheduling and atomic-operation limitations remain tracked in
[plan 01](plans/review-follow-up/01-streaming-budget.md).

Scheduled results in milliseconds, shown as p95 / max (all percentiles and
baseline rows are retained in the linked captures):

| Radius | Scenario | Sync | CPU submission | Whole frame |
| --- | --- | --- | --- | --- |
| 100 | walking | 0.265 / 2.385 | 0.058 / 0.079 | 0.498 / 2.610 |
| 100 | running | 0.192 / 0.430 | 0.053 / 0.141 | 0.422 / 0.645 |
| 100 | teleport | 2.762 / 3.169 | 0.075 / 0.106 | 3.041 / 3.521 |
| 100 | radius | 2.764 / 3.206 | 0.070 / 0.141 | 3.058 / 3.487 |
| 300 | walking | 1.183 / 4.231 | 0.567 / 1.130 | 2.038 / 4.952 |
| 300 | running | 0.909 / 3.691 | 0.272 / 0.379 | 1.368 / 5.214 |
| 300 | teleport | 4.074 / 8.282 | 0.188 / 0.409 | 4.554 / 8.640 |
| 300 | radius | 3.265 / 8.318 | 0.118 / 0.165 | 3.601 / 8.718 |

At radius 300, synchronous teleport and radius-grow whole-frame maxima were
989.488 and 941.510 ms; scheduled maxima were 8.640 and 8.718 ms. Across all
scheduled scenarios the largest frame was 10.281 ms (world-edge travel), and the
largest atomic operation was 3.874 ms, exceeding the 2 ms soft new-work budget.
Maximum final-request loading was 476 budget passes; radius-300 grow needed
461 passes, including the tail after the measured window. These counts describe
convergence work, not measured wall-clock latency: tail passes perform sync only.
Lower frame spikes therefore do not establish immediate terrain availability.
Metadata, queue scans/sorting and retirement remain outside the new-work budget;
their work categories and per-scenario costs are in the raw captures. Follow-up
profiling and scheduling changes remain with plan 01 rather than this audit.

## Limits and pending checks

This is evidence for one accelerated Mesa/AMD device. Other vendors/drivers,
physical keyboard/mouse delivery, visible-window presentation quality and
subjective loading smoothness remain unverified. The scripted camera/movement
checks exercise actual GL rendering but do not replace an interactive playtest.
The soft scheduler budget does not guarantee a maximum frame time; residual
metadata/retirement costs and large-radius convergence latency remain explicit
follow-up work in plan 01. No new renderer lifecycle or culling defect was
reproduced by these fixtures.
