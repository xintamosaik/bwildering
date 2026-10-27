# Stage 3: establish a graphics-independent game owner

Status: implemented, 27 October 2026; GL runtime verification limited by the
unavailable X11 display. Order: 3 of 5. Prerequisite: [stage 2](02-input-and-ui-routing.md).

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

Implemented `game.h` / `game.c` with embedded World, Player and tick ownership,
value configuration, deterministic spawn selection and a fixed 60 Hz command
entry. Initialization and teardown reuse existing world storage; commands and
views add no heap allocations. `GameCommand` contains held movement, aim angles,
one-shot jump/place/remove and placement material. Game validates aim/movement
and retains the existing six-world-unit reach and placement/collision checks.
Environment parsing and error reporting stay in the application.

Player now holds gameplay aim; `view.h` derives a presentation value from a
read-only player borrow and client pending aim. Rendering consumes that value,
performance reads a const world borrow, and renderer synchronization uses the
explicit mutable streaming borrow for existing residency/mesh bookkeeping.
The application and affected fixtures/captures link the new owner normally.

Intentional behavior changes:

- Entering gameplay through one fixed-step command also moves block edits onto
  simulation ticks. Zero-tick frames retain coalesced pending actions; the first
  available tick applies aim, movement, placement, removal, then increments the
  tick. Subsequent catch-up ticks retain held input without replaying presses.
  This is the minimum timing integration needed by the new owner; stage 4 remains
  responsible for broader scheduling/replay tests and timing documentation.
- Pending mouse look affects the view between ticks without changing committed
  gameplay aim. Pause/resume discards uncommitted look and queued actions.
- Initial spawn uses the central voxel's center, including odd world sizes;
  default even-sized world spawn selection is preserved.

Verification:

- `make all` and `make test`: passed. Existing physics, terrain, collision,
  placement, allocation-failure, pool and pause/menu tests remain in place.
- New `test_game` links only game/world/storage and libm (confirmed with `ldd`,
  no GLFW/GLEW/OpenGL). It covers lifecycle, invalid configuration/reservation
  failure, configured seed/size/transform, stable spawn including a one-voxel
  world, jumping, authoritative movement/targeting, placement/removal precedence,
  material/reach constraints and view independence. The routing fixture also
  checks pending look on a zero-tick frame, next-tick commitment and pause discard.
- `make test-sanitize`: default execution encountered LeakSanitizer's fatal
  ptrace limitation. `ASAN_OPTIONS=detect_leaks=0 make test-sanitize`: passed for
  game ownership, storage, physics/terrain, allocation failures and routing under
  AddressSanitizer/UndefinedBehaviorSanitizer. Leak detection remains unverified.
- `make test_renderer streaming_capture visibility_capture terrain_capture
  performance_capture allocation_capture local_vertex_capture -j4`: passed.
- `make test-renderer`: attempted; GLFW could not open X11 display `:0`, so
  runtime GL checks remain unverified. No display/context pass is claimed.
- Every changed C file plus new `game.c` and `tests/test_game.c` checked with
  `clang-tidy -checks=-*,bugprone-narrowing-conversions -- -Wall -Wextra -O2
  -std=c99`, with `-DBW_RENDERER_TEST` for the corresponding fixtures.
  No new warnings; the 62 existing warnings in `tests/test_game_logic.c` match
  a check of its pre-change version. All other changed/new C files are clean.
- `git diff --check`: passed.
