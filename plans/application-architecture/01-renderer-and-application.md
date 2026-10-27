# Stage 1: extract rendering and application orchestration

Status: implemented; real-GL verification outstanding (display unavailable).
Order: 1 of 5. Prerequisite: none.

[Architecture overview and shared verification](../application-architecture.md).

## Problem description

Most rendering, startup and frame orchestration still live in `main.c`.
`application.h` exposes GPU mesh and scheduler implementation state alongside
window, input, world and menu state. UI accesses renderer internals, and several
tests and capture tools include `main.c` to reach private functions. Moving code
without addressing those dependencies would leave the same coupling spread
across more files.

This makes renderer lifecycle changes difficult to review independently and
makes a later headless game/server build depend on application organization.
The first step must establish real translation-unit boundaries while retaining
the existing rendering and failure-verification evidence.

## Implementation

1. Move renderer types and functions from `application.h` and `main.c` into
   `renderer.h` / `renderer.c`, with an internal header only if tests or UI need
   implementation details. Keep shader behavior, mesh layout, budgets, resource
   ownership and error handling unchanged in this stage.
2. Move application initialization, frame coordination and shutdown into
   `application.c`. Leave `main.c` responsible for invoking the application and
   returning its exit status. Keep startup configuration and performance output
   compatible with existing captures.
3. Give UI a small explicit drawing interface for its existing text/rectangle
   needs, instead of exposing the complete renderer just to access GL buffers.
   Keep this specific to the present UI; do not build a general rendering API.
4. Update Makefile dependencies and all tests/capture tools that include
   `main.c`. Link normal module implementations where possible. Keep existing
   failure injection and GL submission observation using a private test boundary
   where required; do not publish every helper or add runtime debug controls.
5. Preserve destruction while the GL context is current, scratch borrowing,
   transactional replacement, retry behavior and bounded scheduler storage.

Temporary exception: renderer synchronization may still control CPU residency
until stage 5. Name and document that coupling rather than hiding it behind a
const-looking interface.

## Detailed extraction sequence

### Inventory the dependency and test surface

Before moving functions, list renderer/application callers, UI accesses to
renderer fields, performance instrumentation accesses, and every fixture that
includes `main.c`. Include renderer, pause-menu, streaming, allocation,
visibility, terrain, local-vertex and performance captures. Preserve their
assertions and observation points rather than dropping difficult fixtures.
Record a passing build/test baseline and any unavailable display checks.

### Define the renderer boundary

Expose only initialization/destruction, viewport/settings changes, world
synchronization and drawing operations actually needed by callers. Pass world,
view and performance information explicitly. Keep shader compilation, section
upload, mesh indexing and culling helpers private. Embedding Renderer state in
the owner is acceptable; an opaque heap-allocated handle is not required.
A private header may hold the concrete layout when that reduces public exposure
without changing the allocation model.

For this extraction, synchronization still needs a mutable World because it
materializes/evicts cache data and updates mesh-sync metadata. Document this
exception and retain it until stage 5. Do not redesign the scheduler while moving
it. Keep UI distance constants in a settings boundary that renderer code can use
without depending on menu implementation.

UI drawing can use a small borrowed draw context or explicit text/rectangle
functions. Specify its lifetime as the current draw call/frame and keep current
buffer capacity, clipping/coordinate assumptions and GL-state behavior. Moving
glyph generation is optional; removing unrestricted renderer access is required.

### Move orchestration without changing frame order

Provide a small application entry point, such as `application_run()`, that owns
its Application storage and returns an exit status. Keep the existing polling,
pause handling, camera update, fixed-step movement, block edits, streaming,
drawing and reporting order. Tick-based edit timing belongs to stage 4.

Audit each initialization failure path against ownership flags. Preserve
renderer teardown before window/context destruction and ensure no queued task or
borrowed scratch survives its owner. Keep world/renderer teardown ordering valid
for the actual data dependencies rather than changing it for style.

Input/UI may still use Application internally in this stage; stage 2 removes
that coupling. Avoid mixing the callback-routing redesign into extraction.

### Adapt verification without widening production APIs

Prefer linking modules normally and testing behavior through their intended
entry points. Existing fixtures intercept GL calls, allocation failures and
platform functions; preserve those controls through narrowly scoped internal
headers or test-build wrappers where normal linkage cannot supply them. Document
which approach each affected fixture uses. Do not expose all static helpers in
public headers merely to make a test compile.

Update every executable's source/header dependencies, including sanitizer and
capture rules. Avoid linking an implementation twice when replacing textual
inclusion with normal compilation. Keep performance reports and capture output
compatible so later comparisons remain useful.

### Review the extraction separately

Inspect the diff for accidental algorithm, shader, timing and capacity changes.
Run existing reference and real-GL checks; record any unavoidable interface-driven
change with evidence that behavior is preserved. No performance gain is promised
by file movement. Start stage 2 only after stage 1's checks are resolved or an
explicit verification limitation is recorded.

## Acceptance criteria

- The application builds with separate renderer and application translation units; `main.c` only enters the application and returns its exit status.
- Renderer implementation no longer includes application internals. UI drawing uses an explicit narrow interface rather than the full Renderer state.
- All existing test and capture targets build with the revised module boundaries; none includes `main.c` to access implementation functions.
- Shaders, mesh bytes/material ranges, draw behavior, streaming budgets, configuration and performance output retain their existing contracts.
- Partial initialization and shutdown release resources exactly once with the GL context current. Existing failure/retry and borrowed-scratch guarantees still hold.
- `make all`, `make test`, relevant sanitizer checks and the real GL suite pass; unavailable GL execution is recorded as an outstanding verification limitation.
- Changed C files have been checked with Clang-Tidy narrowing checks and introduced warnings resolved.

## Validation and completion evidence

Apply the [shared verification requirements](../application-architecture.md#verification-for-implementation-stages), including builds, affected tests and
Clang-Tidy checks. Record commands, results, limitations and any intentional
behavior changes here when implementing this stage.

Implemented on 27 October 2026. No intentional gameplay, shader, mesh, scheduler,
configuration or report-format changes.

Dependency inventory and boundaries:

- `application.c` owns startup, camera updates, frame coordination and shutdown;
  `main.c` only returns `application_run()`. Input callbacks and UI transitions
  retain their existing routing for stage 2.
- `renderer.c` owns shader compilation, GPU meshes, scheduler, visibility and
  drawing. `renderer_internal.h` holds its unchanged embedded layout;
  `application_internal.h` composes application storage. Renderer implementation
  has no application dependency. Performance instrumentation uses the private
  layouts at the same reporting boundaries as before.
- UI previously accessed program/uniform/buffer handles for FPS, overlay and menu
  drawing. It now receives a frame-borrowed `UiDrawContext` and uses only
  begin/triangle-submission/end operations and framebuffer dimensions. Glyph and
  rectangle generation stay in UI, with the same 96-character buffer capacity,
  coordinates and depth/cull restoration. Settings use renderer accessors;
  distance constants live in `view_settings.h`.
- World synchronization intentionally still changes CPU residency, storage limits
  and mesh-sync metadata. The mutable-World contract in `renderer.h` records this
  stage-5 coupling. Scratch borrowing, transactional replacement, retry budgets
  and teardown order are unchanged.

Fixture adaptation (all former `main.c` inclusions removed):

| Fixture | Revised boundary |
| --- | --- |
| Renderer tests | Normal application/renderer linkage; selected private observations in `tests/renderer_test.h`, enabled only by `BW_RENDERER_TEST`; linker wrapping for real GL submissions; existing storage rejection and GL upload injection retained. |
| Pause-menu tests | Normal application/renderer linkage with GLFW linker wrappers; UI/input are still included locally to inspect shared menu layout and drive private callbacks under the same stubs. |
| Streaming capture | Normal module linkage; private budgeted/unlimited synchronization comparison retained. |
| Allocation capture | Normal module linkage; private visible-list collection and existing storage-failure injection retained. |
| Visibility capture | Normal module linkage; private culling/list observations and GL linker wrapping preserve reference ranges and depth comparisons. |
| Local-vertex capture | Normal module linkage; test-only shader/culling helpers preserve the representation comparison. |
| Terrain and performance captures | Normal module linkage; application lifecycle/frame functions are declared in the private application header. |

Verification:

- Baseline `make all test` passed. Baseline `make test-renderer` built, then
  failed at GLFW initialization because X11 display `:0` could not be opened.
- Final `make all test` and all affected capture builds passed (performance,
  streaming, visibility, terrain, allocation and local-vertex). Mesh-query,
  profile and section-storage benchmark targets also build.
- `make test-renderer` still builds but cannot execute without an accessible
  X11/OpenGL context. No GL test or capture execution is claimed as passing;
  real GL drawing, failure/retry and context-lifetime checks remain outstanding.
- `make test-sanitize` encounters a LeakSanitizer runtime limitation: this
  environment uses ptrace, which LeakSanitizer does not support. ASan/UBSan are
  verified separately: `ASAN_OPTIONS=detect_leaks=0 make test-sanitize` passed
  all storage, game-logic, allocation-failure and pause-menu tests. Leak detection
  remains unverified in this environment.
- Every changed C file was checked with
  `clang-tidy --checks=-*,bugprone-narrowing-conversions -- -Wall -Wextra -O2
  -std=c99` (plus `-DBW_RENDERER_TEST` for renderer-observation fixtures). No narrowing warnings
  remain. Existing capture integer-to-float conversions were made explicit.
- Comparing the extracted renderer against the original source after ignoring
  function linkage gives identical code for all existing renderer functions.
  Application diff review confirms unchanged polling, pause, camera, fixed-step,
  edit, sync, draw and reporting order; renderer destruction still precedes
  window/context destruction. No allocations or capacities were added.

