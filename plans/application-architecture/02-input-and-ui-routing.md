# Stage 2: make input capture passive and route it explicitly

Status: implemented; verification and performance correction recorded below. Order: 2 of 5. Prerequisite: [stage 1](01-renderer-and-application.md).

[Architecture overview and shared verification](../application-architecture.md).

## Problem description

Input callbacks currently retrieve the whole Application and can invoke UI
behavior during event polling. UI can reset simulation timing, clear gameplay
queues, change cursor capture and modify visual settings. This distributes
application policy across input, menus and the update loop, making transition
ordering hard to reason about and making device capture difficult to reuse
without the full application.

## Implementation

1. Attach callbacks to input-owned state rather than requiring an Application
   pointer. Record presses and pointer state; callbacks must not invoke UI,
   modify pause state, edit the world or request application shutdown.
2. Route captured input once per application frame: application/menu controls
   first, then gameplay if permitted. UI hit testing receives pointer position
   and dimensions explicitly, rather than querying the window itself.
3. Introduce a small UI action enum/value for resume, quit and view-distance
   changes. UI owns menu navigation state. Application applies actions and owns
   pause transitions, cursor capture, accumulator reset and gameplay-input discard.
4. Keep input remapping, shared bindings and clicks between polls working.
   Preserve click-time pointer coordinates for menu hit testing if events are
   buffered. Use bounded inline storage with documented overflow behavior only
   where ordered events are needed; do not add a general message system.
5. Preserve the transition-frame rule: a click or key that opens/closes a menu
   cannot also place a block or trigger a jump. Discard pending gameplay motion
   and presses on pause/resume while retaining the existing held-action policy.

## Acceptance criteria

- Input includes neither UI nor application internals, and callbacks only record input-owned data.
- Application routes menu/application input before gameplay and applies UI actions at a documented point in the frame.
- UI does not mutate game/world state, simulation timing, gameplay queues or window capture directly.
- Remapping, shared bindings, between-poll clicks and pointer-coordinate scaling remain covered by focused tests.
- Pause/resume and settings/back tests demonstrate that transition input cannot leak into gameplay; held/transient behavior is explicit.
- Any event storage is bounded, has defined overflow behavior and introduces no per-event allocation.

## Validation and completion evidence

Apply the [shared verification requirements](../application-architecture.md#verification-for-implementation-stages), including builds, affected tests and
Clang-Tidy checks. Record commands, results, limitations and any intentional
behavior changes here when implementing this stage.

Implemented on 27 October 2026.

Ownership and routing:

- `input.c` includes only `input.h`. `input_attach` installs an `InputState`
  window user pointer and keyboard, cursor and mouse-button capture callbacks.
  Callbacks only update input storage; they never call UI or application policy.
- Application routes input once after event polling and frame-clock bookkeeping,
  before camera, gameplay queues or simulation. Pause actions take priority over
  menu clicks in that frame. Menu clicks run in capture order and their returned
  actions are applied immediately, stopping at resume or quit.
- `UiState` owns settings navigation. UI returns `UiAction` values for resume,
  quit and view distance; application owns cursor capture, pause state, transient
  discard, accumulator reset and renderer-setting application. UI rendering
  borrows an explicit read-only `UiFrame` snapshot instead of Application.
- Hit testing and hover receive window coordinates and window/framebuffer
  dimensions through `UiPointer`. Layout, drawing and fixture control lookup
  share `ui_menu_buttons`. The headless fixture now links UI and input normally,
  capturing their registered callbacks through GLFW linker wrappers rather than
  including implementation files.
- The framebuffer callback no longer needs an Application window pointer.
  Input captures resize dimensions; application applies a pending resize before
  drawing. Normal gameplay frames perform no window/framebuffer size queries or
  viewport updates. Pointer dimensions are queried only for paused UI.
  This corrects the initial stage-2 implementation's unconditional queries and
  viewport reset after a reported 10–20% FPS regression; measurements below
  quantify the effect in the capture scenario.

Storage and transition behavior:

- The inline click queue retains the first `INPUT_CLICK_CAPACITY` (32) left-click
  positions per frame and drops newer clicks on overflow. Action bits coalesce
  independently, so overflow cannot lose a pause action. No event/frame heap
  allocations were added; callback state lives as long as its application window.
- Frames opening or closing menus cannot run gameplay. Every paused frame and
  pause control frame clears transient presses, mouse motion, menu clicks,
  queued gameplay actions and accumulated simulation time. Held actions remain:
  movement continues on the next gameplay frame and holding jump does not
  synthesize another jump edge. Settings/back navigation remains paused.
- Intentional behavior changes: menu changes occur after polling, and a pause
  action wins over simultaneous menu clicks. Keyboard presses released between
  polls are now buffered too (GLFW repeats are ignored); shared keyboard actions
  still suppress another press while the virtual action was already held.
  Mouse clicks retain their existing explicit-press behavior, including when
  remapped to an already-held action.

Verification:

- `make all` and `make test` passed, including game logic, allocation failure,
  storage and headless routing tests. Focused cases cover remapping/shared
  bindings, held versus transient input, between-poll clicks, passive callbacks,
  ordered settings/increase/back/resume clicks with later pointer movement,
  simultaneous pause/gameplay bindings, pause priority, overflow, gameplay
  isolation and different window sizes/pixel densities.
- `make performance_capture streaming_capture visibility_capture terrain_capture
  allocation_capture local_vertex_capture` passed.
- The initial sandboxed `make test-renderer` could not open X11 display `:0`.
  Re-running with local display access passed the full renderer/application suite
  on AMD Radeon HD 7800 Series, Mesa 26.2.5. Added real-GL checks verify that
  ordinary gameplay draws issue no window/framebuffer size queries or viewport
  updates, and a captured resize updates the GL viewport exactly once. The
  headless fixture verifies resize capture survives gameplay-input discard.
- `make test-sanitize` fails at the environment's unsupported LeakSanitizer/ptrace
  combination. The documented fallback is
  `ASAN_OPTIONS=detect_leaks=0 make test-sanitize`, passed all storage, game
  logic, allocation-failure and routing tests under ASan/UBSan. Leak detection
  remains unverified.
- `clang-tidy application.c input.c ui.c
  --checks='-*,bugprone-narrowing-conversions' -- -Wall -Wextra -O2 -std=c99`
  and the same check on `tests/test_pause_menu.c` and `tests/test_renderer.c`
  with `-DBW_RENDERER_TEST` passed without warnings. These match the affected targets' compile flags.
- `git diff --check` passed.

Performance correction measurements (27 October 2026):

- Built the initial stage-2 frame path and corrected path with identical GCC
  `-O2 -std=c99` flags. Ran the existing `performance_capture` serially with
  `BW_PERF=1 BW_WORLD_SIZE=512`, 1920x1200, swap interval zero, overlay off.
  Four alternating before/after pairs used the same seed and AMD/Mesa context.
  Each entry below is the median of four 30-frame stationary phase means.

| View distance | Frame ms before → corrected | CPU render ms before → corrected |
| --- | --- | --- |
| 25 | 0.298 → 0.297 | 0.086 → 0.026 |
| 100 | 1.668 → 1.603 | 0.165 → 0.067 |
| 300 | 3.155 → 3.032 | 0.277 → 0.149 |

- These captures confirm removal of CPU render overhead and about 4% higher
  whole-frame FPS at distances 100/300. They do not reproduce the entire
  reported 10–20% difference; short full-frame timings remain sensitive to GPU,
  swap and desktop scheduling. An initial 192-wide capture was rejected because
  its scripted traversal leaves that world's bounds; it is not a passing capture.
- A second serial capture compared the corrected path with unchanged stage-1
  commit `ba4cc3e` under the same configuration. Median CPU render times were
  0.027/0.065/0.157 ms for stage 1 and 0.026/0.065/0.154 ms for corrected stage 2
  at distances 25/100/300. Full-frame means varied by +5.3%/-3.5%/+0.1% in FPS;
  no consistent remaining CPU render overhead was observed in that comparison.
