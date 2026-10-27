# Renderer verification

Priority: alongside streaming and edit changes. Status: planned.
Related: [issue 010](../../issues/010-renderer-lifecycle-audit.md).

## Goal

Close the original review's missing-`main.c` verification gap using the full
renderer and an actual OpenGL context. Existing CPU evidence is useful but
cannot establish GPU resource retirement or visual correctness.

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
4. Compare depth coverage and submitted geometry with an uncullled reference
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
