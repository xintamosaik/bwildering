# Landmass profile: round, flat, beach and grass

Status: implemented as experimental beach 01; walking review pending. It fulfills the
[primary landmass archetype](../landmass.md) in the
[creation blueprint](../../README.md).

The first concrete implementation is the
[EXPERIMENTAL 512-block quadrant plan](../../experiments/01-small-beach-quadrant.md),
with a 256-block dry-land diameter. It selects a simple initial version of this
profile. Its subsequent spawn follow-up places the player on the dry beach at
`(170, 2, 170)` after the terrain was finished.

## Intended feel

A broad, rather round patch of land rises gently out of the ocean. Approaching
from water, the player encounters a shallow sandy coast, a dry beach, and then
a grassy interior. Most of the interior is flat and easy to walk across. The
first experiment has no mountains, pronounced hills, steep cliffs, or scattered
offshore fragments.

Round does not require a perfect geometric circle. A radial outline is the
baseline; modest, smooth variation may soften it later without creating deep
bays, narrow necks, or extra islands. Begin without small coastline noise so the
large shape and beach width can be evaluated clearly.

## Shape and surface order

Treat the distance from the landmass's anchor as a way to define successive
zones. The profile owns their widths; these zones do not correspond to chunks.

| Moving inward | Ground shape | Surface |
| --- | --- | --- |
| Open ocean | Existing flat seabed | Existing water over stone |
| Shallow coastal shelf | Continuous rise from the seabed | Sand below the water |
| Shoreline and beach | Gentle transition to a broad dry band | Sand |
| Beach-to-interior transition | Small, gradual rise | Sand gives way to grass over dirt |
| Interior | Broad, mostly level ground | Grass over dirt, with stone below |

Resolve the solid surface first. Fill the land body with stone above the existing
seabed while preserving dark stone below. Replace the upper layers with sand at
the coast or dirt plus a grass cap inland. Fill remaining space up to the existing
water limit with water. Leave air above the resolved surface. Never place grass
on a submerged column or leave water trapped inside the solid land body.

The material transition should follow the coast and its inland distance, not a
global checkerboard or a hard boundary at a planning-square edge. A narrow mixed
transition is optional after the basic bands look right; scattered material noise
is not required for this first profile.

## Heights and the origin

The current generator places water blocks through voxel `y=0` inclusive. Their
upper surface is therefore at `y=1` when block size is 1 and world origin Y is 0.
All ground heights below mean the top of the highest solid block, not its index.

For an initial experiment, consider a dry beach surface at `y=2`, followed by
a grass surface at `y=3` or `y=4`. Keep most of the grass plateau at one height;
at most one block of gentle local variation is enough to test later. These are
proposals, not committed generation constants. Sand depth of 2–3 blocks and dirt
depth of 2 blocks under a single grass cap are also initial proposals.

Use broad transitions rather than a stack of steep concentric walls. The outer
underwater rise can extend well beyond the visible beach. Keep the dry route
simple: few one-block rises, no repeated jumping to cross the plateau, and no
unintended vertical cliff at the waterline.

## Waking on land

The eventual required outcome is a safe, dry spawn on the first landmass. The
current experimental plan centers that landmass at `(256, 256)` in the positive
quadrant. The completed spawn follow-up selects the dry beach at `(170, 2, 170)`;
normal grounding and jumping apply after the one-time placement.

Spawn elevation must come from the solid support query, not the highest occupied
surface that can include water. In particular, literal feet position `(0, 0, 0)`
would remain underwater in the current coordinate convention. In the later spawn
task, select X/Z on the finished landmass first, then choose Y from solid ground.
Allow an ordinary block-centering offset as needed.

Check the entire player footprint, clear space for the body, and a small dry,
mostly flat area around the spawn. At block size 1, a proposed 5×5 clear ground
patch and two blocks of air above it are easy starting criteria. For other block
sizes, use the actual player dimensions. Keep the spawn away from the shoreline.
Resolve the position once; normal movement, gravity, jumping, and water behavior
apply afterward. Do not lock the player's height.

## Dimension experiments

Hold seed, player scale, walking speed, and view distance constant. Current view
distance is 300 world units; at block size 1, much or all of the small presets
may be visible from the center. That is useful for the first shape review and
does not settle the eventual world scale.

| Proposal | Dry-land diameter | Dry beach width | Beach-to-grass transition width | Approximate center-to-shore walk |
| --- | --- | --- | --- | --- |
| Small | 128 blocks | 6 blocks | 8 blocks | 16 seconds |
| Medium | 256 blocks | 10 blocks | 12 blocks | 32 seconds |
| Large | 512 blocks | 16 blocks | 20 blocks | 64 seconds |

Walking estimates use a direct flat route at 4 world units/second and block size
1. The underwater shelf lies outside the dry-land diameter. Beach width is the
dry sand band; transition width is additional distance toward the interior.
Start with medium as a proposal, then vary diameter separately from beach width
and height. Earlier larger landmass presets in the masterplan remain later
experiments; this table is for understanding the first simple shape.

## What to review before expanding scope

- Overhead view: one connected, rather round landmass; no square edges or islands.
- Water approach: a readable shallow coast and beach without a sheer wall.
- Shore-to-center route: sand, then grass, with little jumping and mostly flat land.
- Spawn: dry feet, clear body space, no falling through terrain, and normal controls.
- Streaming: identical coastline and materials across chunk borders and reloads.
- Distance: whether the coast is too close, too far, or too uniformly visible.

The first implementation should establish this profile and collect impressions.
Do not add the deferred archipelago islands as part of that work.
