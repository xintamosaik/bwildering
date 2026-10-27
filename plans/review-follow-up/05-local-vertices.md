# Section-local and compact vertices

Priority: deferred, measurement-gated. Status: planned evaluation.
Related: [issue 007](../../issues/007-section-local-vertices.md).

## Problem description

`SurfaceVertex` stores three absolute floats, costing 12 bytes per vertex and
72 bytes for each six-vertex visible face before other overhead. Large absolute
coordinates may also limit placement precision, though a visible defect has not
been demonstrated. Section-local floats alone do not reduce vertex size, while
compact formats require shader changes and may add per-section draw setup.
Measurements are needed to establish whether either change pays for itself.

## Goal and decision gate

Evaluate section-local coordinates and compression separately. Adopt only for
measured memory, rendering or precision benefits that justify the added cost.

## Implementation

1. Record vertex counts/bytes, upload submission, draw submission and GPU timing
   where supported for mountains, flat surfaces and heavily edited terrain.
   Distinguish payload bytes from measured device bandwidth; do not infer bandwidth
   from CPU upload timings alone.
2. Construct precision fixtures at distant supported coordinates, negative
   positions, translated origins and non-unit scales. Demonstrate any actual seam
   or jitter before claiming a precision defect.
3. Prototype local float vertices plus section origin first. Assess camera-relative
   origin calculation if large absolute shader translations still lose precision.
   Compare added uniform/transform changes against the material-first draw loop.
4. Separately evaluate compact integer local coordinates. Section corners span
   0..16 inclusive; select attribute types and shader conversion explicitly.
   Keep material ranges, winding and world transform semantics unchanged.
5. Adopt the smallest justified change or record a defer decision. Update byte
   accounting, upload formats and captures together if the representation changes.

## Validation and completion

Compare reconstructed face coverage, depth, section seams, culling bounds,
negative/distant placement and edits against the current renderer. Run shared
checks and real GL tests. Record before/after memory, CPU/GPU costs and precision
results in issue 007; a documented defer decision completes the evaluation.
