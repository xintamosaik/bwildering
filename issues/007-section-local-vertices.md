# 007 — Section-local and compact vertices

Status: evaluation complete; production adoption deferred (27 October 2026).

[Plan 05](../plans/review-follow-up/05-local-vertices.md) requested a measurement
gate, including an acceptable documented defer decision. The original issue file
was absent; this entry records the completed evaluation.

The reproducible experiment is `make local_vertex_capture && ./local_vertex_capture`.
It needs a working X11 display and OpenGL 3.3 context. Run serially. Raw captures:
[run 1](../plans/review-follow-up/local-vertex-measurements/run-1.csv),
[run 2](../plans/review-follow-up/local-vertex-measurements/run-2.csv),
[run 3](../plans/review-follow-up/local-vertex-measurements/run-3.csv).
[Shared validation](../plans/review-follow-up/local-vertex-measurements/validation.txt)
and [real GL validation](../plans/review-follow-up/local-vertex-measurements/renderer-validation.txt)
record the checks. No production vertex format, shader, upload accounting or
streaming byte budget changes are adopted.

## Experiment and limits

Measured on Intel Core i5-6500, AMD Radeon HD 7800 Series (radeonsi, pitcairn,
ACO), OpenGL 4.6 / Mesa 26.2.5-arch3.1, GCC 16.2.1 20260810, `-O2 -std=c99`,
X11 `:0`, 320×240, hidden window, swap interval zero, no overlay or gameplay
performance instrumentation. Seed: 1260202551. Three complete serial runs;
60 samples per format per scene after four warmups, rotating format order.

The opt-in harness keeps all prototypes outside the production renderer:

- Absolute floats: the current mesher, 12 bytes per vertex.
- Local floats: unscaled section-local float triples and a section-origin uniform,
  12 bytes per vertex; no payload saving.
- Local integers: three `uint8_t` components, inclusive corners 0..16,
  `glVertexAttribIPointer(0, 3, GL_UNSIGNED_BYTE, 3, 0)`, shader `uvec3` input and
  explicit `vec3(position)` conversion. No normalization. Three bytes per vertex.
- Relative integers: the same compact payload, double precision subtraction of
  the eye from the section's world origin before narrowing to float, and a
  translation-free view matrix. The subtraction is included in each timed draw.
- Untimed reference: CPU double precision world reconstruction minus the eye,
  uploaded as floats and drawn with the translation-free view matrix.

Production meshing runs on a borrowed view with origin zero and scale one to
obtain exact grid coordinates, then again with the actual world transform.
The harness verifies every decoded compact/local vertex against the grid and
every baseline vertex against `origin + grid * scale`. This preserves face
coverage, ordering, winding, material ranges and six vertices per face. Encoding
is performed before upload timing; the `encoding` rows include combined float
and byte conversion plus assertions, and are **not** integrated mesher timings.
A future adoption would emit local vertices directly instead of converting an
already built absolute mesh.

All formats use the same material-first ordering, ranges, world AABBs and
precomputed visible-section list. The measured draw loop excludes visibility
collection, clear, query retrieval and GPU drains. Local formats add one origin
uniform per submitted material range and one scale uniform per frame. CPU draw
timing includes program/matrix/material setup, binding and submission. The
prototype follows the production draw order but is not a measurement of the
whole application frame or its synchronization scheduler.

Upload timing is the summed CPU time for `glBufferData`, attribute setup and
enabling, on newly created section buffers. GL object creation, CPU encoding,
buffer readback and explicit drains are outside it. Upload order rotates by
section. These are cold resource submission costs with driver overhead, **not
measured device bandwidth**. Each driver buffer's actual size and readback bytes
are checked. GPU columns use supported 64-bit `GL_TIME_ELAPSED` queries around
the draw loop, read after the CPU timer stops; zero timer bits means unavailable.
GPU sample quantization and a single device limit conclusions about small costs.

Fixtures: a deterministic 128×128 region around the highest sampled mountain
within [-1024,1024]², that same region after 1,024 removals and 1,024 additions
across materials/section boundaries, and a flat 128×128 two-material surface.
These are bounded representation fixtures, not a maximum-residency memory test.

## Memory and submission results

Values below are medians across the three captures; CPU/GPU columns are the
median of each run's p50, in milliseconds. Payload is the requested vertex bytes
retained per format, excluding driver overhead, metadata and spare capacity.

| Scene | Vertices | Absolute/local float bytes | Compact bytes | Draws / extra origin uniforms |
| --- | ---: | ---: | ---: | ---: |
| Mountains | 1,458,468 | 17,501,616 | 4,375,404 | 260 / 260 |
| Edited | 1,491,840 | 17,902,080 | 4,475,520 | 443 / 443 |
| Flat | 199,680 | 2,396,160 | 599,040 | 50 / 50 |

Absolute format has zero origin updates. Both local formats and the relative
variant save no draw calls. Compact payload is 75% smaller: 18 rather than 72
bytes per six-vertex face; local floats remain 72 bytes per face. CPU scratch
in the prototype remains float storage and does not demonstrate a CPU memory
saving. Five simultaneous experimental GPU copies are retained for comparison;
the table describes each copy, not the experiment process's total residency.

| Scene | Format | Upload CPU ms | Draw CPU p50 ms | Draw GPU p50 ms |
| --- | --- | ---: | ---: | ---: |
| Mountains | Absolute | 2.335890 | 0.052039 | 0.194962 |
| Mountains | Local float | 2.149171 | 0.080107 | 0.194962 |
| Mountains | Local u8 | 4.783882 | 0.082259 | 0.194962 |
| Mountains | Relative u8 | 3.233533 | 0.081079 | 0.194962 |
| Edited | Absolute | 1.708716 | 0.077976 | 0.213037 |
| Edited | Local float | 1.762779 | 0.123325 | 0.213185 |
| Edited | Local u8 | 3.750678 | 0.120428 | 0.213185 |
| Edited | Relative u8 | 2.811151 | 0.123034 | 0.213185 |
| Flat | Absolute | 0.177521 | 0.010653 | 0.085185 |
| Flat | Local float | 0.188003 | 0.015328 | 0.085185 |
| Flat | Local u8 | 0.112074 | 0.014814 | 0.085333 |
| Flat | Relative u8 | 0.097788 | 0.015083 | 0.085333 |

The captures also retain CPU/GPU p95. Local formats consistently increase CPU
submission in these scenes; there is no demonstrated GPU improvement. Compact
uploads improve the flat fixture but regress the mountain/edited fixtures on
this driver. The data does not establish why, nor how a different driver or
integrated mesher would perform.

## Precision, seams, culling and edits

Depth is read after real GL rasterization with depth testing and back-face
culling enabled. Local float/u8 paths must match absolute coverage exactly and
depth within 1e-6 at every fixture. The relative path must match the independent
CPU double reference coverage exactly and depth within 1e-5. All cases pass.
The existing world-space AABB frustum/distance predicates also preserve unculled
depth within 1e-5 for every representation, including the distant fixtures.
Geometry and material changes in the edited scene are rebuilt from authoritative
world data. Precision fixtures remove a voxel between their two camera poses.

The precision fixtures contain a 32×32 surface two blocks thick with all eight
materials, section seams, signed X/Y/Z, and the following transforms:

- Near zero, scale 1.
- Region starting at WORLD_COORD_MIN + 31 (-1,048,544), including distant Y.
- Region starting at WORLD_COORD_MAX - 62 (1,048,513), including distant Y.
- Negative region starting at -32, translated origin (-57.25,31.5,-83), scale 2.5.

Every fixture uses supported public world creation/edit APIs. Both poses are
rendered, with a 0.125-block camera movement and a removal between poses.
Near/translated/scaled absolute and local paths show no coverage differences or
depth errors above 1e-5 against the double reference. Mountains/edited terrain
show one pixel above 1e-5 (maximum 1.4722e-5), with identical coverage; local
floats and local u8 reproduce the baseline result.

Distant negative poses differ from the double reference by 27/34 coverage pixels,
with maximum depth error 0.00620586. Distant positive poses differ by 64/50
coverage pixels, with maximum depth error 0.00722331. Absolute, local float and
local u8 have the same errors; the relative u8 variant has zero coverage/depth
differences in these captures. This demonstrates a distant-coordinate rendering
precision discrepancy, not an independently isolated section seam or an
interactive jitter measurement. It combines absolute view-matrix cancellation
and float camera-target construction; no claim is made that the stored vertex
format alone caused it or that local coordinates alone fix it.

## Decision

Defer production adoption of both local floats and compact integers. Local floats
have no byte saving and do not improve the absolute-translation precision
results. Compact integers save substantial payload, but add origin updates to
the material-first loop, increase measured CPU submission, and show no GPU
benefit here. The current bounded streaming workload has no demonstrated vertex
memory exhaustion, so this evaluation does not establish that the saving
justifies changing meshing, upload accounting, scheduler budgets and shaders.

Revisit compact vertices if actual working-set pressure or a GPU vertex-fetch
bottleneck appears. Repeat the capture on the target device and measure direct
local emission in the mesher before adoption. Investigate camera-relative
rendering separately if distant edited regions become a gameplay requirement;
carry physics/camera precision and world-space culling through that evaluation.
The distant discrepancy is retained as a limitation, not silently declared fixed.
This documented defer decision completes plan 05's evaluation gate.
