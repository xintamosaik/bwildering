# Landmass relief: hills and optional mountains

Status: first implementation in [experimental quadrant 02](../../experiments/02-hills-and-mountain-quadrant.md);
dimension review pending. Extends the
[primary landmass](../landmass.md) after the accepted size and placement of
[beach experiment 01](../../experiments/01-small-beach-quadrant.md).

## Separate responsibilities

Hills and mountains are distinct landform generators. The landmass selects
whether to include either of them. A flat landmass remains valid; hills do not
imply a mountain, and a mountain does not require hills across the entire island.

| Feature | Shape and feel | Surface | Proposed first dimensions |
| --- | --- | --- | --- |
| Hill | Broad, rounded rise, long approaches and rounded crest | Grass over dirt over continuous stone | 6–10 blocks above the interior; 40–60 block influence radius |
| Mountain foothills | Broad transition from surrounding ground to the mountain | Mostly grass over dirt and stone | 8–12 blocks of rise; 60–75 block influence radius |
| Mountain core | Higher, more concentrated summit or short ridge | Grassy lower slopes, exposed stone on steep or high ground | Summit 32–48 blocks above the interior; core radius 25–40 blocks |

All dimensions are experiment proposals in voxel units, not permanent settings.
The current grassy interior surface is Y=3; a 40-block mountain rise therefore
means a surface at Y=43. Broader world-scale mountain proposals in the masterplan
remain separate from this first small landmass experiment.

## Composition order

1. Resolve the existing ocean, coastal shelf, beach, and interior baseline.
2. Derive an inland relief mask from distance to the coast. Taper relief to zero
   before the beach so the accepted coastline and spawn remain usable.
3. Sample the optional hill field: a small set of broad, smooth mounds rather
   than unrelated random heights in each column.
4. Sample each optional mountain, including its own foothill envelope. Its core
   rises continuously out of that envelope, never directly out of flat ground.
5. Blend mountain relief into the hill field, then quantize the final surface
   once. Do not stack independently rounded layers.
6. Choose surface materials from the final height and slope. Fill the complete
   solid body with stone up from dark stone, then apply surface caps. Preserve
   the existing ocean fill outside the solid body.

For the first experiment, each field expresses a nonnegative rise above the
same interior baseline. Use the greater of hill relief and mountain relief,
rather than adding their full heights. This keeps peak height independent of
an accidental hill under the mountain. The mountain's own foothill envelope
ensures a transition even when hills are disabled. A later smooth maximum can
soften the crease where two fields meet if it is visible in the experiment.

The inland mask applies to the composed relief. Restrict placement so a mountain
has enough space for its base; shrinking a tall mountain against the beach mask
would create steep walls. If the base does not fit, omit or reduce the feature.

## Hill shape

Start with smooth radial or elliptical mounds whose rise and first derivative
reach zero at their influence boundary. An example normalized radial profile is
`(1 - r*r)^3` for `0 <= r < 1`, with zero outside. Combine a bounded number of
mounds by their maximum for the first experiment. Avoid small-scale noise until
the broad slopes and walking distances have been reviewed.

Tune rise and radius together: broad hills should have long terraces separated
by occasional single-block rises. Current movement requires jumping at those
rises; smooth height sampling alone does not make voxel stairs walkable without
jumping. Measure climb interruptions before considering movement changes.

## Mountain shape and materials

A mountain owns its center, extent, foothill rise, total summit rise, and core
shape. A broad smooth base plus a narrower core is sufficient initially. Define
the total summit rise explicitly so changing foothill height does not silently
add that much extra elevation to the peak.

Start with one peak; add asymmetric ridges only after scale and access feel right.
At least one approach should have one-block rises on a traversable route, even
if other faces are steep. Grass belongs on gentle lower slopes. Bare stone is
chosen using the composed surface's slope and height, not a blanket rule that
every mountain column loses grass. Exact thresholds need the first experiment.
No snow, caves, overhangs, erosion, or new block types are required initially.

## Generation boundaries

Keep feature descriptors and height sampling in the generation module, separate
from storage, meshing, rendering, and player movement. Use bounded descriptors
with static or arena storage. Sampling must be a pure function of the world
seed, stable feature identity, and world coordinates; visiting chunks in a
different order must not move a hill or mountain.

Sample neighboring coordinates to estimate slope, including across chunk edges.
Do not clamp the feature to the current chunk. Direct queries, materialization,
and regeneration after eviction must agree. Existing edits continue to override
generated blocks. A future saved-world policy must identify the relief version.

## Suggested experiments

Compare three presets on the same accepted island: flat control, hills only,
and hills with one mountain. Also check mountain-only sampling to prove its own
base works. Keep coastline, spawn, seed, view distance, and movement fixed.

Begin with one hill about 8 blocks high and 50 blocks in radius. Review it from
the beach, walk across its crest, and count required jumps. Then try a mountain
with a 40-block total rise and a 70-block base radius, positioned well inland.
These were the initial proposals. Experiment 02 records the implemented
dimensions, including its broader mountain core and optional launch presets.

Validate the untouched beach and spawn, continuous stone foundation, cap order,
dry grass, route access, chunk-border agreement, seed repeatability, edit
retention, generation time, and visible mesh cost. Record playtest impressions
before increasing height, count, or shape complexity.
