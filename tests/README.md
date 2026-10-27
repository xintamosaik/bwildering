# Test design

Tests should state the behaviour they protect and tolerate changes that preserve it.
Grey-box tests are useful here: inspecting state is appropriate when ownership,
invalidation, or recovery is the behaviour under test.

| Area | Assertions to use |
| --- | --- |
| World size | Test the smallest supported size, the documented maximum, and the first invalid sizes. Rejected replacement must preserve the old world. Keep these separate from the default gameplay size. |
| Application startup | Check configured bounds, safe spawn, grounding/jumping, and visible terrain. Mesh-slot counts do not establish that terrain renders. |
| Terrain generation | Check seed independence of the experimental profile, exact foundation/ocean boundaries, sand/soil caps, a connected round outline, valid materials, and hidden water/water faces. Add different-seed variation when seeded shapes are introduced. Check exposed mesh surface area rather than a fixed vertex total. Exact hashes would impose a separate terrain-format compatibility requirement that this game does not currently have. |
| Palette | Check valid, distinct colours and invalid-input behaviour. Exact RGB values are an art choice. |
| Meshing | Use small, explicitly constructed shapes for exact geometry assertions. Counts such as 36 vertices for one cube belong here, rather than in application smoke tests. |
| Dirty sections | Check the identities of affected sections, including unaffected neighbours and identical writes. The right number of wrong neighbours is still a bug. |
| Allocation failure | Discover allocation points from a successful run. Each injected failure must leave either the complete new world or the intact old world. Check contents as well as transactional ownership. |
| Menu interaction | Find the displayed control by label, then dispatch a real mouse callback at its position. Exercise different window sizes and pixel densities. Keep reachable terrain in the fixture so leaked gameplay clicks would change it. |
| Pausing | Compare gameplay values explicitly, rather than raw struct bytes. Verify frozen movement/view, input isolation, and resumed movement. |
| Rendering | Inspect depth coverage to distinguish geometry from a coloured background. Compare views during movement/jumping and after landing, without storing driver-specific screenshots. Water tests compare color changes while preserving opaque depth and restoring GL blend/cull/depth-write state. |
| Streaming | Compare procedural reads and meshes with fully generated terrain. Verify edits made before materialization, eviction/regeneration, revisits, allocation-failure retries, and bounded residency as the view moves or shrinks. |

Named fixture sizes, such as the 192-block generator scenario, are independent of
the game's default world size. They should remain fixed when that default changes.
Movement speeds, jump envelopes, crouch offsets, section boundaries, and mesh
formats are explicit behaviour or focused component contracts; avoid removing
those assertions just to make a change pass.

`test_hidden_section_neighbor_edits` compares optimized meshes with an independent
voxel-face enumerator before removal, after exposure and after restoration. It
checks face positions, outward winding and every material across all six section
boundaries at negative and nonnegative Y. Eager and streamed fixtures cover both
resident neighbours and edits before materialization, with exact dirty-section
identity checks. See [issue 004](../issues/004-mesh-candidate-correctness.md).

`make test` runs logic, reservation-failure, reusable-storage, and headless menu interaction tests.
`make test-renderer` additionally requires an X11 display and OpenGL 3.3 context.
Its application checks also guard against window/framebuffer size queries and
redundant viewport updates on gameplay draws, and verify one viewport update
for a captured resize.
The [renderer audit](../renderer-audit.md) records the verified GL device,
reproduction commands, handle/buffer ownership and real-context depth/submission
checks. New fixtures cover unculled reference parity for transformed negative
section seams, clip boundaries and maximum distance, unique handle transfer and
GL deletion, old geometry during pending edits, fast-departure cancellation and
convergence to an independently drained world. Raw serial travel captures include
sync, CPU submission and whole-frame p50/p95/p99/max distributions.
`make test-sanitize` runs the non-graphical suites with ASan and UBSan. Environments
that cannot run LeakSanitizer under tracing can use
`ASAN_OPTIONS=detect_leaks=0 make test-sanitize`; this does not check leaks.

The headless menu suite also checks virtual action rebinding, opposing movement,
shared bindings, one-shot presses, buffered mouse clicks, and a remapped pause
action. Bindings currently live in `InputState.keyBindings` and `mouseBindings`;
`input_init` installs defaults, and zero removes a binding. `held` tracks active
actions and `pressed` tracks new presses for each frame. Binding persistence and
a controls settings screen are not implemented.

The headless menu suite substitutes window input and time, but calls the real
application update and registered callbacks, with input and UI linked as separate
translation units. It also verifies passive capture, click-time positions,
ordered menu actions, pause priority, shared pause/gameplay bindings and bounded
click overflow. The first 32 left clicks per frame are retained; newer clicks
are dropped while gameplay/action bits continue to coalesce. UI owns the shared
layout used to locate displayed controls. It does not verify GLFW's delivery of physical
keyboard/mouse events or the visual appearance of menu text.

Tick timing is checked in `test_game` and the headless menu suite. Live input
samples latest held movement/run/crouch/swimUp and pending aim for every catch-up tick.
Jump/place/remove each occupy one pending flag: repeated presses coalesce, zero-tick
frames retain them, and the next tick consumes them once. Commands supplied directly
to Game are already assigned to ticks. Tick order is aim, movement/collision,
placement, removal, then tick increment; CPU edits are immediate, meshing may lag.
The local menu stops ticking and discards pending input/aim and accumulated time
on capture transitions; a future remote session would keep ticking with its menu open.

Tests compare the real frame scheduler with assigned commands through coalesced
presses, held input, catch-up, landing, frame clamping and clock reversal. They
also cover next-tick targeting, movement before edits, pending view look,
pause/resume and recapture. A 120-command replay compares player and voxel state
under frequent zero-tick frames and irregular catch-up schedules, using a 1e-6
world-unit player tolerance (well below collision separation). The actual GL
presentation and physical event delivery still require a display/manual check.

`make performance_capture` builds the opt-in scripted performance runner. See
[the performance note](../performance-note.md) for scenarios,
controls, metric definitions, captured results and their limits.

`make visibility_capture` builds the opt-in comparison of the original rendering
loop, the final visibility list with material-first drawing, and section-first
drawing. It checks exact submitted ranges, triangle counts and depth parity
before measuring CPU submission and whole-frame distributions. See the
[visibility experiment](../plans/review-follow-up/visibility-measurements.md)
for reproduction and results.

`make streaming_capture && ./streaming_capture` runs the fixed-seed travel
benchmark in a real GL context. It checks a bounded unedited working set and
reports per-stop CPU/GPU residency, memory, materialization/mesh/upload timings
and retirements. See [the streaming policy and measurements](../streaming.md).

`./streaming_capture --sync` measures stationary and walking synchronization
with instrumentation disabled, separating frame-path overhead from cold travel.

Lifecycle invariants are exercised by `test_chunk_lifecycle_invariants` in the
logic suite, the edited-materialization and regenerated-mesh failure cases in
`test_allocation_failures`, and `test_streamed_retirement_after_failed_sync` in
the renderer suite. The fixtures use both edited and unedited columns and compare
exact block values, mesh vertices and material ranges across departure/revisit.

| Transition | Ownership and dirty-state contract checked |
| --- | --- |
| Materialize | Missing CPU sections commit together; existing edits, section links, counts, flags and materialized bounds survive failure. |
| Edit/rebuild | CPU blocks are authoritative immediately. CPU meshing borrows sections and leaves dirty flags unchanged. Successful GPU upload clears dirty; failed upload preserves old VAO/VBO, buffer bytes and ranges for retry. |
| Grow renderer storage | Failed metadata growth preserves the record array and live handles. Failed visible-index growth retains its storage and clears submission counts, even if metadata has already committed. |
| Leave | All departing procedural CPU columns are released; edit differences remain in the world overlay. All departing GPU geometry retires, including an old mesh awaiting retry. |
| Revisit | Cells regenerate from seed plus overlay; edits and removals survive. Rebuilt GPU buffer bytes and material ranges match CPU snapshots, and uploaded sections become clean. |

Failures are transactional per chunk materialization or section replacement,
not per whole renderer pass. Residency retirement and earlier successful uploads
may commit before a later allocation/upload fails. Run the renderer suite with
`make test-renderer` to check actual GL ownership and buffer readback; the default
headless suite checks CPU ownership and regeneration independently.

`make mesh_query_benchmark mesh_query_profile` builds headless mesh-query
experiments. Run `./mesh_query_benchmark 500` for uninstrumented before/after
build times and `./mesh_query_profile 50` for query, lookup and column-sampling
costs/counts. Both compare exact mesh vertices and material ranges against the
frozen pre-005 mesher. See [mesh-query measurements](../mesh-query-performance.md)
for fixtures, instrumentation limits and the storage decision.

`make section_storage_benchmark && ./section_storage_benchmark` measures live
live section requests and retained pool capacity on three mountainous regions and
prints generation timings and complete voxel checksums. See
[section storage](../section-storage.md) for before/after results and transition
tests.

`./streaming_capture --baseline` and `./streaming_capture --frames` compare
synchronous loading with the gameplay scheduler across fixed-step stationary,
walking, running, teleport, reverse, radius-change and world-edge scenarios at
radii 25/100/300. Run serially; GPU drains stay outside measured frame loops.
`test_stream_budget` in the GL suite uses operation/byte limits to check bounded
queue exhaustion, eventual convergence, aging, nearest work, revision refresh,
edit/upload/metadata failure retries, old drawable geometry, shrink/cancellation
and revisits. Converged GPU bytes and material ranges match synchronous meshes;
a stationary completed view starts no further operations.

The edit-overlay capacity fixture visits 1,024 edited columns, fills all 65,536
entries, rejects a new difference, reclaims/reuses a baseline-restoration slot,
and reconstructs every column twice. Allocation-failure tests cover first-arena
reservation and every materialization allocation with partial caches and after
full edited eviction, checking overlay ownership and entry bytes as well as
resident state. The GL travel fixture visits 60 edited locations under the same
49-column residency bound as unedited terrain.


`make allocation_capture && ./allocation_capture` captures opt-in per-category
allocation/reservation counts, reuse, requested/live/peak capacity, slack, RSS,
GPU requests and sync timing distributions on a real X11 GL context. See
[allocation and reusable storage](../allocation-storage.md) for raw before/after
captures, reproduction commands, capacity/exhaustion contracts and test evidence.
`test_storage` exercises independent node/payload limits, partial staging rollback,
reuse after eviction/compaction, world isolation and borrowed mesh overflow/parity.
Failure injection operates on logical reservations, including reused slots.

`make local_vertex_capture && ./local_vertex_capture` runs the opt-in, serial
real-GL comparison of absolute floats, local floats, local unsigned-byte triples
and camera-relative unsigned-byte triples. It checks decoded geometry, material
ranges, actual buffer bytes, depth/coverage and culling against independent
references, then records payload bytes, CPU upload/draw submission and supported
GPU query timings. Precision fixtures cover distant signed coordinates, section
seams, translated origins, scale 2.5, camera movement and edits. See
[issue 007](../issues/007-section-local-vertices.md) for raw captures, limits and
the completed defer decision. This experiment does not change production vertices.

Water movement checks in `test_game` cover buoyancy, slower travel, view-directed
swimming, held Space ascent, Shift diving, drag on entry, solid floor/ceiling
collision, leaving water, and stable floating at several update rates. Input
checks keep held swimming separate from a one-shot land jump.

`make test_beach_generation && ./test_beach_generation` checks experimental beach
01: positive-quadrant bounds, round outline, grass/dirt/sand layers, foundation,
water fill, direct/eager/streamed agreement, mesh parity, and edits across eviction.
Cardinal and diagonal movement fixtures swim onto the waterline foothold and use
two normal jumps to reach the grass. The beach spawn is checked for dry support and normal jumping.
`make terrain_capture && ./terrain_capture` adds overhead, approach, shore, and
interior views under `/tmp/world-generation-beach-*.ppm` using a real GL context.
