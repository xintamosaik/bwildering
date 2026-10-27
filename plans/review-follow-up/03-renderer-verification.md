# Renderer verification

Priority: alongside streaming and edit changes. Status: implemented and validated
on local accelerated OpenGL (27 October 2026).
Related: [issue 010](../../issues/010-renderer-lifecycle-audit.md).

## Problem description

The original review lacked `main.c`, leaving renderer integration unverified.
At plan creation, the renderer suite compiled but could not initialize X11
display `:0`. CPU tests and a successful build could not establish correct GL
handle ownership, resource retirement, culling or displayed geometry through
edits and streaming transitions. Those paths required evidence from the full
renderer running with an actual OpenGL context.

## Goal

Close the verification gap using the full renderer and an actual OpenGL context.

## Implementation

1. Record compiler, GL vendor/renderer/version and display setup. Run `make all`,
   `make test` and `make test-renderer`. Use a supported local display or available
   virtual display; do not treat display initialization failure as a test pass.
2. Audit `renderer_prepare_world()`, `renderer_sync_section()`,
   `renderer_upload_section_mesh()`, `renderer_sync_view()` and destroy paths.
   Record who owns each GL handle and metadata buffer before/after success,
   failure, departure and world destruction. Check copied handles have one owner.
3. Extend existing renderer fixtures only for missing behaviors: metadata and
   visible-index growth failure, failed replacement followed by departure,
   empty retirement markers, repeated edits and radius shrink/grow. Read back
   buffer bytes/material ranges and verify deleted GL objects where appropriate.
4. Compare depth coverage and submitted geometry with an unculled reference
   for negative coordinates, translated/scaled worlds, camera/frustum boundaries,
   maximum view distance and edited section seams. Prefer tolerances appropriate
   to depth values over driver-specific screenshots.
5. As staged streaming lands, verify valid old geometry during pending edits,
   no submission of retired handles, cancellation on fast movement and eventual
   equality with a fully drained reference. Rendering temporary missing chunks
   must follow the loading policy in plan 01.
6. Run walking, running, teleport and view-distance scenarios with measured sync,
   submission and whole-frame distributions. Distinguish lifecycle correctness
   from responsiveness; use plan 01 for scheduling fixes.

## Validation and completion

- Store reproducible commands, device details and results in a renderer audit
  note linked from issue 010 and `tests/README.md`.
- Track any reproducible defect separately with trigger, expected behavior and
  regression coverage. Keep unavailable runtime checks explicitly pending.
- Complete when ownership transitions, culling and streaming scenarios pass on
  a real context and remaining frame-time limitations are measured and tracked.

## Completion evidence

The [renderer audit](../../renderer-audit.md) records compiler, GL device and
display details, reproducible commands, the ownership transitions of all audited
paths, real-context results and remaining verification limits. It is linked from
issue 010 and `tests/README.md`.

Existing failure/retirement fixtures were retained. New fixtures compare depth
coverage and actual submitted ranges against an independent unculled draw loop
for negative coordinates, translated/scaled worlds, clip boundaries, maximum
view distance and repeated seam edits. They check unique handle ownership after
metadata transfer and actual deletion at destruction. Pending edits keep old
geometry drawable; fast departure cancels work and retires handles; staged
revisits converge to independent synchronous worlds with matching GPU bytes,
material ranges and depth.

Shared builds/tests, the extended real-GL suite, ASan/UBSan with LeakSanitizer
disabled, and Clang-Tidy narrowing checks passed. Serial baseline/scheduled travel
captures now include separate synchronization, CPU submission and whole-frame
percentiles for walking, running, teleport and radius changes at 25/100/300.
Remaining scheduling spikes and loading latency stay tracked in plan 01;
other devices and interactive presentation remain unverified, as recorded in
the audit.
