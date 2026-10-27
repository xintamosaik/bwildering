# Stage 4: make command timing independent of render frames

Status: planned. Order: 4 of 5. Prerequisite: [stage 3](03-game-ownership.md).

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

Implementation has not started; no acceptance criterion is claimed as verified.
