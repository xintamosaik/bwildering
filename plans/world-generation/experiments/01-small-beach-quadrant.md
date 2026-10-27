# Experimental implementation plan: small beach quadrant

Status: **EXPERIMENTAL — implemented; walking review pending.** The terrain is
available with these initial dimensions. The spawn follow-up is now implemented
at `(170, 2, 170)`. These are not permanent world-generation limits.

Walking feedback: the current size is sufficient and the distance from `(0, 0)`
is acceptable. Retain the dimensions and placement. Surface and other future
feedback can still be evaluated without moving or enlarging this landmass.

Related: [creation blueprint](../README.md),
[round beach-and-grass profile](../archetypes/landmass/round-flat-beach-grass.md),
and [masterplan](../../world-generation-masterplan.md).

## Goal and scope

Implement one round, mostly flat landmass with a sandy beach and grassy interior
inside the smallest proposed planning quadrant: `512 × 512` blocks, extending
from horizontal `(0, 0)` toward positive X and Z. The player will walk the result
to decide whether this scale is sufficient.

The quadrant is the planning area, not the size of the dry land. Use a dry-land
diameter of 256 blocks for this experiment: half the quadrant width, preserving
the proportions of the smallest landmass previously proposed for a 2048-block
quadrant. This also matches the existing profile's medium landmass preset.

Implement only this landmass. The ocean and the two future archipelago areas
remain the existing ocean baseline. Do not implement islands, a complete
four-area placement system, mountains, coastline noise, vegetation, structures,
or persistence as part of this experiment.

**Spawn follow-up completed:** after the terrain was finished, the player was
placed near the requested `(170, 1, 170)`. Solid support raises the feet to the
dry beach surface at `(170, 2, 170)` instead of placing them inside sand. Initial
aim, gravity, swimming, view distance, and landmass placement remain unchanged.

## Experimental dimensions

All coordinates and lengths below are in voxel units. Heights name the exclusive
top of solid ground, not the index of its highest block. Convert through the
existing world origin and block size for rendering and physics.

| Parameter | First experiment |
| --- | --- |
| Planning quadrant | `0 <= x < 512`, `0 <= z < 512` |
| Quadrant extent in 16-block chunks | 32 × 32; this is not a residency requirement |
| Landmass center | `(256, 256)` |
| Dry-land radius / diameter | 128 / 256 blocks |
| Flat grass interior radius | 118 blocks |
| Dry sand beach width | 10 blocks |
| Additional waterline foothold width | 4 blocks |
| Outer coastal shelf radius | 192 blocks |
| Unchanged ocean margin at the nearest quadrant sides | 64 blocks |
| Grass surface height | `3` |
| Dry beach surface height | `2` |
| Waterline foothold surface height | `1`, matching the water surface |
| Existing deep seabed surface height | `-32` |
| Sand cap | 3 blocks |
| Interior soil cap | 2 dirt blocks underneath 1 grass block |

Keep these values together in one named experimental profile in the generation
module. They must be easy to retune without altering world bounds or unrelated
terrain code. The 512-block extent is not a new world-size cap; retain the current
world outside this quadrant and generate chunks on demand as before.

## Shape and materials

Sample radial distance `r` from each column center `(x + 0.5, z + 0.5)` to the
landmass center. Use a plain circle initially; all seeds may share this first
profile. Random variation is not required to evaluate the dimensions.

| Radius from center | Solid surface | Surface material and behavior |
| --- | --- | --- |
| `r < 118` | `3` | Grass over two dirt blocks; flat interior |
| `118 <= r < 128` | `2` | Three-block sand cap; dry beach |
| `128 <= r < 132` | `1` | Three-block sand cap; waterline foothold |
| `132 <= r < 192` | Smooth descent toward `-32` | Sand cap where the seabed rises above its baseline; water above |
| `r >= 192` | Existing `-32` | Unchanged stone seabed and ocean |

For the outer descent, an initial formula is `t = (r - 132) / 60`,
`u = t*t*(3 - 2*t)`, and `solidTop = floor(1 - 33*u)`. At the outer radius,
the result meets the existing seabed. Where quantization still gives the baseline
height `-32`, retain the baseline stone material. Keep this calculation inside
the generator, independent of chunk loading and rendering.

The four-block foothold at water height is deliberate. It lets a swimmer emerge
onto sand, become grounded, and then make one ordinary jump onto the dry beach.
Another one-block rise reaches the grass. Avoid a two-block wall directly from
water to dry sand: the present swimming movement is not a climbing system.
Do not add auto-step or change jump strength to compensate for this terrain.

This deliberately simple first profile has two one-block rises before a broad
flat interior. A separate gradual beach-to-grass transition, small hills, or
material blending can be evaluated later if the walking review calls for them.

## Voxel composition

1. Preserve dark stone at `-128 <= y < -64`.
2. Preserve the stone seabed and extend stone continuously upward wherever the
   landmass raises the solid surface.
3. Apply the selected sand cap or dirt-and-grass cap to the resolved ground.
4. Fill remaining space above solid ground through voxel `y=0` with water.
5. Leave air above the higher of solid ground and the water surface.

Retain the distinction between occupied height and solid support height. A
submerged column's highest occupied material is water; a dry interior column's
highest occupied material is grass. Do not put water inside the land body or
grass below the water surface. Existing edits must still override generated
blocks and survive eviction/reconstruction.

## Implementation sequence

1. Add the experimental profile parameters and radial sampling in
   `world_generation.c/.h`. Use `const` data and bounded automatic or existing
   arena storage; no per-column heap allocations or mutable global random state.
2. Extend the column description only as needed to represent surface caps and
   distinguish solid and occupied heights. Keep surface selection in generation.
3. Integrate through existing direct queries and materialization. Check that
   exposure classification includes raised coast, grass, and opaque surfaces
   next to water. Storage and streaming continue to own their current operations.
4. Add focused sampling, materialization, and edit/reload checks, plus review
   captures of the overhead outline, shore approach, beach, and grass interior.
5. Deliver the terrain with its experimental parameters documented. The original
   terrain-only step preserved spawn; the subsequent beach-spawn follow-up is done.

No new archetype registry, biome framework, fluid simulation, shader effect,
unbounded-world rewrite, or save format is needed for this profile.

## Verification before walking

- Confirm the grass interior, dry sand, waterline foothold, submerged shelf, and
  unchanged deep ocean at representative points on each side of the circle.
- Check cardinal and diagonal approaches. The land must form one connected body,
  with reachable water-to-sand and sand-to-grass routes.
- Check that the full influence fits inside the positive quadrant and becomes
  the unchanged ocean before reaching its boundary. Negative coordinates and
  columns at or beyond X/Z 512 retain the baseline.
- Compare direct, eager, and streamed block results across chunk seams. Include
  material transitions, partial chunks, and edits before/after eviction.
- Preserve the foundation, static water level, water transparency, swimming,
  land jumping, and the default 300-unit view distance.
- Run the relevant build, world, streaming, allocation-failure, and rendering
  checks. Check changed C files with Clang-Tidy's
  `bugprone-narrowing-conversions`; resolve introduced warnings.

The origin remains outside the landmass's influence and remains water. The
production spawn is now on dry beach at `(170, 2, 170)`; the terrain and origin
were not moved. Origin queries should still return the unchanged ocean.

## Walking review and decision

The spawn follow-up now supplies solid beach support and body clearance near
`(170, 2, 170)`. Walk the experimental landmass using these routes:

| Route | What to judge | Rough flat walking reference at block size 1 |
| --- | --- | --- |
| Water → foothold → beach → grass | Can the player enter land without a cliff or repeated jumping? | Two single-block rises after swimming |
| Dry shore → center | Is there enough interior space? | About 32 seconds |
| Across the dry landmass | Does it feel like a place to explore? | About 64 seconds |
| Around the beach | Is the coast substantial and readable? | About 3 minutes |

Times use 4 world units/second and exclude swimming, jumping, and detours.
Crossing the whole 512-block planning area is not a 512-block land walk; it also
contains ocean. With visibility at 300, most or all of this landmass can be seen
from the center. Keep visibility fixed while judging this first size.

Record whether the landmass feels too small, the beach too narrow or wide, the
interior too empty, and the shore too abrupt. Retune one dimension at a time.
Only after that review decide whether to keep this size or enlarge the quadrant
and landmass. Keep the plan marked experimental until the walking feedback has
been incorporated; do not add islands to make a small landmass feel larger.

## Implementation evidence

- The profile and cap sampling live in
  [world_generation.c](../../../world_generation.c), with one grouped
  `BEACH_EXPERIMENT_01` configuration. Storage, renderer, gameplay movement, spawn,
  and the 300-unit view distance required no behavior changes in the terrain-only step.
- `make all` and `make test` pass. `test_beach_generation` checks the profile,
  unchanged ocean bounds, round connected outline, sand/soil depths, foundation,
  signed sampling, scaled/translated queries, synthetic/resident mesh parity,
  and edits across eviction/reconstruction.
- The measured outline contains 51,468 dry columns, including 43,748 grass
  columns, plus 3,288 waterline foothold columns. These are observations for this
  preset, not required counts for later experiments.
- Actual movement tests reach the foothold, beach, and grass from all four
  cardinal and four diagonal directions using swimming and two ordinary jumps.
  Collision checks allow for the voxel-centered contour rather than assuming
  a player's center stops exactly on the analytic circle.
- The existing real-GL renderer/application suite passes. `terrain_capture`
  now writes `beach-overhead`, `beach-approach`, `beach-shore`, and
  `beach-interior` PPM captures under `/tmp/world-generation-*.ppm`; these views
  were reviewed on the X11/AMD OpenGL context. Capture cameras belong to a
  separate fixture and do not change the live spawn.
- The beach test also passes ASan/UBSan with leak detection disabled for the
  environment. Clang-Tidy's `bugprone-narrowing-conversions` found no introduced
  warnings in the changed C files.

Initial size and placement feedback has been accepted, and the beach-spawn
follow-up is complete. Keep this experimental profile available for further
material and walking feedback without changing the accepted dimensions.
