# World generation and persistence masterplan

Status: agreed direction, 27 October 2026. The first flat-foundation step is
implemented, followed by static water; hierarchical landmasses and disk persistence
remain planned.
Dimensions below are experimental presets, not settled world constants.

## First playable step: flat foundation

Reset generation before experimenting with dimensions. Generation now lives in
`world_generation.c/.h`; block types are shared through `block_type.h`.
The generation module owns sampling, eager/streamed creation, the legacy shuffled
platform fixture, and its seeded random helper. Callers include its header
explicitly; gameplay logic no longer includes or implements generation. Section
allocation, edit merging, and materialization transactions remain in world storage.
The legacy platform reuses fixed-capacity scratch rather than allocating a
separate shuffle buffer for each call.

- Dark stone: -128 <= y < -64 (64 blocks, voxel indices -128 through -65).
- Stone: -64 <= y < -32 (32 blocks, voxel indices -64 through -33).
- Water: -32 <= y <= 0 (33 blocks); air above y=0 and below the foundation.
- Player starts at the configured sea-level origin (normally y=0). Water now
  provides buoyancy and swimming; land retains normal gravity and jumping.
- Remove old noise relief, mountain/surface rules, and the grassy spawn search.
- Keep block edits, eager/streamed agreement, and normal movement/collision.

Remove the current finite-world limits as subsequent architecture work: replace
world-sized chunk metadata with a bounded resident chunk index backed by persistent
chunk records, decouple generated vertical bounds from storage bounds, and review
coordinate/render precision. Do not describe raising the 6,144-block constant as
unbounded streaming. The flat-foundation step still uses existing world bounds
and memory safety checks. Persistence capacity and visible distance remain explicit
budgets even when the world no longer has a fixed gameplay edge.

Verification for the flat-foundation step: `make all`, `make test`, and
`ASAN_OPTIONS=detect_leaks=0 make test-sanitize` passed. Leak detection had to be
disabled because LeakSanitizer fails under the environment's tracing. All changed
C files were checked with Clang-Tidy's `bugprone-narrowing-conversions`; no warnings
occur on changed lines (existing unrelated test warnings remain). Renderer and
terrain-capture targets compile. After restoring gravity and jumping, `make all`
and `make test` passed again, as did the renderer/application smoke tests run
through the IDE on the real X11/AMD OpenGL context. Updated spawn tests verify
falling from sea level, landing, jumping, and landing again. Clang-Tidy reported
no narrowing warnings in the changed movement/game/renderer test files.

## Static water step

Water uses ordinary stored block IDs. It fills empty columns above the seabed
through voxel y=0 inclusive, so the top face is at y=1. `TerrainColumn` separates
the solid surface from the occupied water surface. A future landmass replaces
water with stone; dark stone and the stone seabed remain underneath.

Material validity, collision, and face visibility are distinct: water is valid
and passable, water/water faces are hidden, and opaque faces beside water remain
visible. `world_surface_height` includes water; `world_support_height` ignores it
for grounded spawning. The initial static-water step left movement unchanged;
the swimming step below now handles immersion. Flowing water, drowning, and
water-level propagation remain outside this step.

The small GLSL 330 shader in `water_shader.h` adds blue-green transparency,
animated ripple normals, grazing-angle sky color, and a soft sun highlight.
Geometry remains fixed to voxel boundaries. The renderer draws opaque materials
first, then water sorted by section distance, without depth writes; it restores
blend, cull, and depth-write state before UI drawing. Underwater opaque geometry
receives distance fog based on the current flat ocean plane. Complex intersecting
transparent shapes and cave-specific water volumes need further work when those
features exist; section sorting is sufficient for the current flat ocean.

Water-step verification: `make all` and `make test` passed. The real-GL renderer
suite passed, including a water test that changes the submerged floor material
to verify transmission, compares opaque depth, and checks restored GL state.
Targeted game and streaming runs passed ASan/UBSan with leak detection disabled.
Clang-Tidy checked all changed C files; no new narrowing warnings were introduced.
Surface and underwater captures were reviewed on the X11/AMD context.

## Swimming movement

Movement measures actual water intersecting the player's body rather than using
only the global ocean plane. Partial immersion blends land and swimming speeds.
Fully submerged travel starts at 2 world units/second (2.8 with the run binding),
and forward/back movement follows the view underwater. Near the surface it stays
horizontal. These speeds are initial tuning values, not final travel dimensions.

- Hold Space/the jump action to swim up; hold Shift/the crouch action to dive.
- Release vertical controls to let buoyancy and drag settle the body partially
  submerged, with eyes above the surface. Height is not locked.
- Water damps incoming falls. Floors and ceilings still collide normally.
- Outside water, the existing gravity, jumping, running and crouching remain.
- Held `swimUp` is distinct from the transient land-jump press. The live input
  path and tick commands preserve that distinction, including pause and rebinding.
- Wet updates use bounded steps for buoyancy stability on long updates. No water
  movement path allocates memory. Buoyancy, drag and controls do not simulate flow.

Checks cover seeded/edited water, partial immersion at signed chunk boundaries,
scaled/translated worlds, swimming speed, view-directed diving, held ascent/descent,
incoming falls, floor/ceiling collision, return to surface, normal air gravity,
and agreement at 30/60/144 updates per second and during a long wet update.

Swimming verification: build, headless tests, input/pause tests, and the real-GL
renderer/application suite passed. The focused swimming/game tests also passed
ASan/UBSan with leak detection disabled. Clang-Tidy found no introduced narrowing
warnings. Dry jump/landing and held-key timing checks remain in the existing suites.

## Intended world

The world is predominantly ocean, with substantial stretches of open water,
large landmasses, and groups of smaller islands. Land has hills and mountains,
with continuous stone connecting it to the seabed. Begin with dark stone,
stone, water, and air; surface materials can follow after the shapes feel right.

Use large squares to organize planning, and successive zoom levels to refine
their decisions. A planning square owns descriptions; its edges do not constrain
coastlines or mountains. Planning regions, storage regions, and voxel chunks
are independent units and need not have the same dimensions.

## Responsibilities

| Owner | Responsibility |
| --- | --- |
| Landmass planner | Coarse ocean/land distribution, region categories, landmass centers, bounds, shapes, and regional mountain placement. |
| World generator | Resolve the plan into column descriptions and block types. Proposed module: `world_generation.c/.h`, extracted from `game_logic.c`. |
| World storage | Resident sections, block queries, player edits, and bounded memory ownership. |
| Streaming coordinator | Schedule chunk loading/generation and eviction within budgets. |
| Persistence | Encode/decode binary world metadata and chunk snapshots; preserve saved terrain and edits. |
| Renderer | Display resident world data. |

Generation must work without graphics or mutable global random state. Prefer
pure coordinate queries and caller-owned bounded buffers or arenas. A generated
block queried before materialization must match the materialized block.

This plan extends the boundaries in [application architecture](application-architecture.md).
Its saved-chunk baseline supersedes the assumption that seed plus a session-only
edit overlay is sufficient to reconstruct every evicted chunk. Integrating that
change is an explicit implementation stage, not an existing capability.

## Planning hierarchy

1. **World scale:** a seeded coarse field establishes broad oceans, continental
   regions, and archipelago regions. Evaluate locally; do not allocate a complete
   world map. The logical world extent is separate from its visible/resident area.
2. **Planning regions:** classify large squares as open ocean, large landmass, or
   archipelago. Neighboring classifications follow the broader field rather than
   independent coin flips. Open ocean is the dominant outcome.
3. **Landmass descriptions:** derive stable centers, sizes, orientation, shape
   seeds, bounding boxes, and mountain regions. Shapes can cross square borders.
4. **Local refinement:** derive irregular coastlines, seabed relief, hills,
   valleys, and ridges. Detail must preserve broad ocean passages and landmass
   interiors rather than overturning the parent plan.
5. **Voxel chunks:** sample the result into the existing section representation.

For each chunk, query all nearby descriptions whose bounds can influence it.
Bound maximum landmass extent and refinement displacement so this neighborhood
has a known maximum size. Use consistent signed-coordinate division at borders.

Low local landmass density may favor additional candidates. Any spacing rule
must compare deterministic candidates, never only already explored landmasses.
One bounded option is to reject a candidate near a higher-priority raw candidate;
avoid recursive acceptance dependencies across unbounded neighborhoods. This
spacing refinement is optional until category-based placement has been evaluated.
Exploration order, eviction, and restart must not change placement.

## Ordered terrain composition

1. Fill the bottom levels with dark stone.
2. Add a relatively thin stone seabed with gentle height variation.
3. Fill above the seabed up to a fixed sea level with water; leave air above it.
4. Add island/continental stone continuously from the seabed to the planned land
   surface, replacing water where necessary while retaining the dark foundation.
5. Refine the land surface with hills and mountain ranges. Usually reduce relief
   toward coasts; allow selected cliffs. Mountains belong to planned landmasses.
6. Optionally add beaches, soil, grass, or snow in a later surface-material pass.

These are semantic priorities, not mandatory full-chunk overwrite passes. A
column can resolve directly to dark stone below the foundation top, stone below
the final terrain surface, water below sea level, and air otherwise. Keep solid
surface height distinct from water surface height for spawning and queries.

## Binary persistence

Save actual generated chunks as snapshots, including subsequent edits. Saved
chunks take precedence over procedural regeneration. Unvisited chunks need no
block payload. Landmass plans can initially be derived from the seed; persist
descriptions if they become editable or cease to be fully reproducible.

- World metadata: save-format version, seed, generator version, generation
  settings, logical bounds, and stable block-type ID definitions/mapping.
- Region files: group nearby chunk records to avoid one filesystem entry per
  small section. Choose grouping independently from terrain planning dimensions.
- Chunk records: signed coordinates, revision, section directory, encoding,
  encoded and decoded sizes, and integrity checks.
- Uniform sections: one block ID for the whole section, including air or water.
- Mixed sections: palette of block IDs plus packed per-voxel palette indices.
  Optional lossless compression follows only when it reduces payload size.
- Define integer widths and byte order explicitly; never serialize pointers,
  enum memory layouts, or raw C structs. Air removals are real saved values.

Load a saved chunk if present; generate only when it is absent. Corrupt or
unsupported saved data is an error, not permission to regenerate over edits.
Keep modified data recoverable until its write succeeds. New snapshots also
need successful persistence before being discarded if they are to remain frozen
against future generator changes. Separate save-dirty state from mesh-dirty state.

Write replacements transactionally, retaining the previous valid data until
commit; specify file/directory durability and interruption recovery during format
design. Validate bounds and sizes before decoding into bounded staging storage.
Failed loading must preserve the active world. Keep old generator versions
available for unexplored chunks, or require an explicit world migration policy.

The existing [persistence issue](../issues/011-persistent-edit-storage.md) records
the current session-only overlay and its failure-handling requirements.

## Dimension experiments

Use blocks as the primary unit. Keep physical block size, movement speed, field
of view, seed set, and view distance fixed during each comparison. These presets
explore horizontal scale first; they do not assert that the current renderer can
show a whole landmass at once.

| Measurement | A: compact | B: spacious | C: expansive |
| --- | --- | --- | --- |
| Planning square side | 512 | 1,024 | 2,048 |
| Typical small-island diameter | 48–128 | 96–256 | 192–512 |
| Typical large-landmass diameter | 256–640 | 512–1,280 | 1,024–2,560 |
| Desired typical open-water gap between shores | 128–384 | 256–768 | 512–1,536 |
| Initial target ocean area | 85% | 85% | 85% |

Landmass diameter may exceed its owning square. Gap ranges are measured targets,
not guaranteed distances. Coverage, size, and spacing interact: tune placement
and measure all three instead of assuming these ranges imply 85% water.

Start with B. Compare A and C using the same relief settings: sea level 0,
world bottom -128, dark stone through -80 exclusive, typical seabed around -64
with about 8 blocks of variation, hills 16–40 above sea level, and mountain peaks
96–160 above sea level. Preserve a stone layer everywhere. Then independently
compare low and high relief, and 75%, 85%, and 92% ocean coverage. All values are
starting hypotheses; the final configuration must enforce valid layer ordering.

### What to experience and record

- Fixed routes: island shore to center, hill to mountain, island to neighboring
  shore, and a route across planning-region boundaries. Include several fixed
  seeds and spawn on land; do not judge only a seed selected for attractive views.
- Compare an overhead land/water map with shoreline and summit views. Check
  recognizability, ocean scale, coast regularity, slopes, and visible seams.
- Record actual route times, water-gap distributions, land coverage, landmass
  diameters, and height/slope distributions. Straight-line travel estimates are
  only scale references, not substitutes for traversable routes.
- Record generation/loading stalls, resident sections, mesh cost, and save sizes
  and latency. Oceans must remain cheap through uniform section encoding.
- Current view distance defaults to 100 world units, with a maximum setting of
  300. Large gaps can therefore hide the opposite shore. First hold visibility
  fixed; then compare visibility independently before changing island spacing.
- Current walking speed is 4 world units/second. At block size 1, a flat
  256-block route takes about 64 seconds; 1,024 blocks about 4 minutes 16 seconds.
  Basic swimming now uses 2 world units/second when fully submerged, with speed
  blending near the surface; boats and final ocean travel tuning remain undecided.
  Do not treat walking speed as swimming speed.
- Water is now translucent and passable, with basic buoyancy and swimming.
  Evaluate swimming routes separately from walking; boats and final ocean travel
  tuning still need their own experiments.
- The current horizontal world limit is 6,144 blocks per side and generated
  vertical range is [-128, 384). These constrain current playtests, not the
  intended hierarchy. Sample larger planning maps without materializing them;
  expanding the playable world needs separate storage/coordinate review.

## Implementation sequence and evidence

1. Extract generation with existing terrain behavior preserved. Validate existing
   reference behavior, allocation limits, and unloaded/resident query agreement.
2. Add seeded coarse planning and diagnostic maps for A/B/C. Check exploration
   order independence, negative coordinates, cross-region influence, and seams.
3. Implement foundation, seabed, water, and land relief; add selectable experiment
   settings and repeatable routes/captures. Record impressions before selecting
   permanent dimensions. Integrate safe land spawning and solid/water queries.
4. Specify and implement binary metadata and chunk round trips, then integrate
   saving with streaming and the edit overlay. Snapshot unedited generated chunks
   as well as edited chunks. Verify air removals and saved/generated boundaries.
5. Validate malformed input, interrupted writes, failed saves/loads, restart,
   eviction/reload, generator compatibility, and bounded memory exhaustion.
6. Tune encoding/compression and rendering from measurements; add surface
   materials after scale and landforms have been evaluated.

Keep changes reviewable and the game usable between stages. Run appropriate
build, world, streaming, persistence, and renderer checks for the affected stage;
check changed C files with Clang-Tidy's `bugprone-narrowing-conversions` and resolve
introduced warnings. No implementation stage is complete merely because its
plan or experiment preset exists.
