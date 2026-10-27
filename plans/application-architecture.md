# Application boundaries and preparation for client/server ownership

Status: proposed implementation plan, 27 October 2026. No implementation changes
are included in this document. Implement the stages in order as separate,
reviewable changes; keep the application usable after each stage.

## Objective and limits

Make input, menus, simulation and rendering understandable independently, while
keeping the existing single-process, single-threaded application. Establish an
explicit path from player intent to authoritative gameplay changes so a local
or remote server can replace the direct simulation call later.

This work does not introduce sockets, threads, separate executables, protocol
serialization, prediction, replicated worlds, an entity framework, a generic
event bus or a service registry. It does not redesign terrain storage, meshing,
save files or allocation policy. A smaller main file is a consequence of useful
boundaries, rather than the completion criterion.

## Intended ownership

| Owner | State and responsibility |
| --- | --- |
| Application | Window/context lifetime, frame clock, simulation accumulator, routing, pause transitions and subsystem coordination. |
| Input | Device bindings, held actions, transient presses and pointer motion. No menu or world mutation. |
| UI | Menu navigation/layout, hit testing and menu drawing; returns application actions. |
| Game | World and player, tick number, spawn rules and authoritative movement/block actions. No GLFW or OpenGL dependency. |
| Renderer | GPU resources, visual settings, mesh cache, culling and drawing. |
| Streaming coordinator | Desired CPU residency, bounded materialization scheduling and coordination with mesh retirement. |

Application owns these objects and their initialization/shutdown order. Keep
existing pool/arena lifetimes explicit. Use embedded state or one owner-lifetime
reservation; do not add per-event or per-command heap allocation. An internal
application header may compose subsystem state, but input, UI and game code must
not include it to reach unrelated mutable state.

Game owns the logical world; CPU materialization is a cache of seed plus edits.
The streaming coordinator may change that cache through world APIs, never the
logical edits. The renderer may borrow world data during a call; it must not
retain section pointers across eviction or mutate gameplay state.

## Implementation steps

Each step has its own problem description, implementation tasks and acceptance
criteria. Implement them in order as separate reviewable changes. Stage 1 gives
a more detailed extraction sequence because it establishes the module and test
boundaries needed by the later work.

| Order | Plan | Main outcome |
| --- | --- | --- |
| 1 | [Renderer and application extraction](application-architecture/01-renderer-and-application.md) | Separate translation units and narrow drawing interfaces, preserving behavior. |
| 2 | [Input and UI routing](application-architecture/02-input-and-ui-routing.md) | Passive callbacks, explicit routing and application-owned transitions. |
| 3 | [Game ownership](application-architecture/03-game-ownership.md) | Graphics-independent game state and command entry. |
| 4 | [Tick command timing](application-architecture/04-tick-command-timing.md) | Defined action consumption and simulation order. |
| 5 | [World streaming](application-architecture/05-world-streaming.md) | CPU residency policy independent of GPU synchronization. |

## Verification for implementation stages

- Run `make all` and `make test` after each stage. Add focused behavior tests for
  routing, command timing and headless ownership; avoid tests of file movement.
- Run `make test-renderer` for extraction, drawing, residency and GPU lifecycle
  changes. Build the affected capture targets as well. A missing display/context
  is a reported verification limitation, never a passing GL test.
- Run `make test-sanitize` when changing ownership, event storage, command
  lifetime or streaming. Retain existing allocation-failure and pool tests.
- Check every changed C file with Clang-Tidy's
  `bugprone-narrowing-conversions` using actual build flags. Resolve introduced
  warnings and retain const-correctness and explicit range-appropriate casts.
- Keep storage capacities, exhaustion behavior and borrowed lifetimes explicit.
  Do not add per-frame allocation. Preserve existing terrain, mesh and collision
  reference tests rather than replacing them with self-comparisons.
- Record completion evidence and any intentional behavior changes in the corresponding step plan.
  Do not report a stage complete if its affected fixtures still depend on an
  obsolete inclusion/build arrangement.

## Later client/server work enabled by these boundaries

The future server can own Game and consume GameCommand values. The client can
own input, UI, view and renderer. A local session can call the server directly in
the same thread before considering bounded queues or separate execution.

Remote play remains a separate project: assign command sequences/player IDs,
validate commands, define snapshots and revisioned voxel updates, synchronize
seed plus generator version, handle interest regions for multiple players, and
add prediction/reconciliation and interpolation as needed. Wire data must be
explicitly encoded; current structs and pointers are not a network protocol.

The present renderer may still read the local authoritative world through a
read-only borrow. A remote client will need its own world cache and update path;
this plan makes that replacement localized but does not pretend to implement it.
Server simulation residency and client visual residency will then be separate
policies. Save-file durability and session persistence also remain separate work.

Stop after stage 5 and reassess actual needs before adding any server wrapper,
snapshot format, duplicate world or transport interface.
