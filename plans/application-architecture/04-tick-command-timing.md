# Stage 4: make command timing independent of render frames

Status: implemented, 27 October 2026; GL runtime verification limited by the
unavailable X11 display. Order: 4 of 5. Prerequisite: [stage 3](03-game-ownership.md).

[Architecture overview and shared verification](../application-architecture.md).

## Problem description

Movement uses fixed simulation ticks, but mouse look changes player state per
rendered frame and block actions run after all catch-up movement, even when no
tick ran. A gameplay action therefore has no single tick assignment. A future
server, command recording or repeatable simulation test needs a defined action
order and consumption policy.

## Implementation

This stage deliberately changes edit timing. Previously, block actions run once
per rendered frame after all catch-up movement, even if no simulation tick ran.
After this stage, they run in a defined simulation tick.

1. Keep the application frame clock, current 60 Hz fixed step, frame-delta clamp
   and accumulator policy. Game advances one tick per call and never reads a
   platform clock. Do not add a second scheduler.
2. Retain one-shot actions until the next tick. Consume each pending action once;
   held movement/run/crouch apply to every catch-up tick. Preserve the current
   coalescing policy: multiple pending presses of one gameplay action represent
   one action until consumed. Document this explicitly rather than implying an
   unbounded history of clicks.
3. For live input, sample held state and latest aim for the next available tick;
   subsequent catch-up ticks use that state without replaying one-shot actions.
   This is a defined sampling policy, not timestamped reconstruction of events.
   Commands supplied directly in tests/replays are already assigned to ticks.
4. Define tick order: apply command aim, move/collide, attempt placement, then
   removal, and advance the tick number. Preserve existing placement-before-
   removal precedence. CPU edits take effect in that tick; meshing can lag.
5. Keep responsive local mouse look separate from committed simulation aim:
   derive the view from current pending aim, commit it in the next command, and
   discard pending motion on capture transitions. Rendering must not write back
   into authoritative player state.
6. Document pause as an application/session policy: the current local game stops
   ticking; a future remote session would keep ticking while its menu is open.

## Acceptance criteria

- Every executed movement, jump and block action belongs to a simulation tick with the documented order.
- Zero-tick frames retain pending actions; catch-up ticks consume one-shot actions once and apply held input consistently.
- Multiple pending presses of one action coalesce according to the documented bounded policy.
- Tests cover simultaneous place/remove, targeting aim, pending mouse look, frame clamping and pause/resume.
- The same preassigned tick-command sequence produces equivalent player/world state under different render schedules, using justified float tolerances.
- Local view remains responsive between ticks without modifying committed simulation aim; visual loading may still lag accepted CPU edits.

## Validation and completion evidence

Apply the [shared verification requirements](../application-architecture.md#verification-for-implementation-stages), including builds, affected tests and
Clang-Tidy checks. Record commands, results, limitations and any intentional
behavior changes here when implementing this stage.

Stage 3 already introduced the fixed-tick command entry and pending view aim as
part of Game ownership. This stage makes that live sampling policy explicit,
samples capture state once per frame, keeps held input immutable during catch-up,
and adds timing/replay regression coverage. No second scheduler or new allocation
is introduced. The 60 Hz step, 0.25-second clamp and residual accumulator are retained.

Live input uses the latest aim and held movement/run/crouch for the next available
tick and all remaining catch-up ticks. Jump/place/remove use exactly three pending
flags; multiple presses coalesce until consumed, with no click history or event
timestamp reconstruction. A zero-tick frame consumes nothing. The first available
tick consumes each pending action once, in Game's aim, movement/collision,
placement, removal, tick-increment order. Direct commands are already tick-assigned.

Pending look derives the view without changing committed player aim. Local pause
stops simulation; pause/resume discards pending motion/actions and accumulator
remainder, and recapture reseeds the mouse. This is session policy: a future
remote session would continue ticking while the local menu is open.

The intentional edit-timing change described above landed with stage 3: block
edits now occur in a simulation tick, including aim and movement from that command,
rather than once per rendered frame. CPU edits are visible immediately to later
ticks; GPU meshing may still lag. This stage preserves that behavior.

Coverage:

- Real application update/callback fixture: repeated presses across zero-tick
  frames coalesce; latest aim and held state are sampled at consumption; jump,
  place and remove run once through catch-up; run/crouch/movement remain held.
  Holding jump through landing does not trigger another jump.
- Simultaneous placement/removal preserves the original target. A later coalesced
  remove deletes only the nearest of two targets despite multiple catch-up ticks.
- A ten-second frame advances only 15 ticks and preserves raw timing; the
  accumulator retains its fractional remainder. Clock reversal adds no time.
- Pending view aim remains responsive with zero ticks, commits next tick, and
  is discarded on pause. Long pause/resume frames execute no ticks, incur no
  catch-up debt, and the first recaptured mouse position produces no motion.
- Game targeting uses command aim after movement: crossing the six-unit reach
  boundary allows removal in that tick. A 120-command replay with jump, movement,
  aim, run/crouch and edits produces equivalent player and voxel state with
  frequent zero-tick frames or irregular multi-tick catch-up frames. The 1e-6
  world-unit player tolerance is below the 0.001 collision separation; discrete
  state and voxel materials compare exactly.

Verification:

- `make all` and `make test`: passed, including existing physics, terrain,
  collision, storage and allocation-failure tests.
- `make test-sanitize`: LeakSanitizer cannot run under this environment's ptrace.
  `ASAN_OPTIONS=detect_leaks=0 make test-sanitize`: passed all five suites under
  AddressSanitizer/UndefinedBehaviorSanitizer. Leak detection remains unverified.
- `make test-renderer`: built, but runtime could not open X11 display `:0`.
  No GL runtime pass or visual responsiveness check is claimed.
- `make streaming_capture visibility_capture terrain_capture performance_capture
  allocation_capture local_vertex_capture -j4`: passed.
- All changed C files checked with `clang-tidy
  -checks=-*,bugprone-narrowing-conversions -- -Wall -Wextra -O2 -std=c99`,
  adding `-DBW_RENDERER_TEST` for `tests/test_pause_menu.c`: no warnings.
- `git diff --check`: passed.
