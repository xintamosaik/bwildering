# 010 — Renderer lifecycle audit

Status: verified on local accelerated OpenGL (27 October 2026).

The original review lacked `main.c` and could not establish GPU lifetime or
visual correctness. [Renderer verification](../renderer-audit.md) now records
the full renderer ownership audit, exact reproduction commands, compiler/device
and display details, real GL lifecycle/depth/submission results and limits.

Regression coverage lives in `tests/test_renderer.c`: transactional metadata and
upload failure, empty retirement markers, failed replacement then departure,
radius shrink/grow, unique ownership after metadata transfer, explicit GL object
deletion, independent unculled depth/range comparisons for transformed negative
section seams and clip boundaries, pending-edit rendering, fast-movement
cancellation and staged convergence against an independently drained world.

`make all`, `make test`, `make test-renderer`, ASan/UBSan (LeakSanitizer disabled)
and Clang-Tidy narrowing checks passed. Raw baseline/scheduled travel captures
include synchronization, CPU submission and whole-frame percentiles.

Remaining responsiveness limitations belong to
[plan 01](../plans/review-follow-up/01-streaming-budget.md) and
[streaming measurements](../streaming.md). Other GL devices and interactive
presentation checks remain explicitly unverified in the audit note.
