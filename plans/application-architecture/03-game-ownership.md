# Stage 3: establish a graphics-independent game owner

Status: planned. Order: 3 of 5. Prerequisite: [stage 2](02-input-and-ui-routing.md).

[Architecture overview and shared verification](../application-architecture.md).

## Problem description

World and player lifetime, spawn selection and gameplay mutations are
coordinated directly by the application. Player state also carries camera data
used for both gameplay direction and presentation. There is no single game
boundary that can be advanced by a headless caller, which would complicate both
focused simulation tests and future server ownership.

## Implementation

1. Add `game.h` / `game.c` containing a Game state with World, Player and a tick
   counter, plus initialization, destruction and one fixed-step update entry.
   Reuse `game_logic` and world APIs rather than rewriting physics or storage.
2. Move world configuration/spawn selection and gameplay mutation behind this
   owner. Pass seed/bounds/block size as configuration values; keep environment
   parsing and reporting in the application.
3. Define a plain-data GameCommand for one tick: movement, gameplay aim,
   held run/crouch and one-shot jump/place/remove actions. Include the material
   selection if needed by placement. Game chooses/validates reach and gameplay
   constraints; device/window state never enters the game API.
4. Separate gameplay aim from presentation-camera state. Movement and ray
   targeting use authoritative aim. The client-side view derives its camera
   from game state, with pending local look available for responsive rendering.
   Keep camera smoothing, bobbing and movement interpolation out of this stage.
5. Replace direct application calls to movement and block-edit functions with
   the game entry point. Rendering and instrumentation borrow explicit read-only
   state. Streaming gets only the mutable world access needed for cache work.

## Acceptance criteria

- A Game can be initialized, advanced and destroyed in a test linked without GLFW, GLEW or OpenGL.
- Game owns World, Player, spawn rules and tick count; configuration enters as values rather than environment/window queries.
- Gameplay changes enter through the game API, with no device state or rendering resource in GameCommand.
- Movement and block targeting use gameplay aim; presentation derives a view without writing authoritative state.
- Existing physics, terrain, collision, placement and failure-injection tests retain their guarantees.
- No server wrapper, serialization, duplicate world or transport interface is introduced.

## Validation and completion evidence

Apply the [shared verification requirements](../application-architecture.md#verification-for-implementation-stages), including builds, affected tests and
Clang-Tidy checks. Record commands, results, limitations and any intentional
behavior changes here when implementing this stage.

Implementation has not started; no acceptance criterion is claimed as verified.
