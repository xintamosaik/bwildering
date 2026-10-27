# Stage 2: make input capture passive and route it explicitly

Status: planned. Order: 2 of 5. Prerequisite: [stage 1](01-renderer-and-application.md).

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

Implementation has not started; no acceptance criterion is claimed as verified.
