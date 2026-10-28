# EXPERIMENTAL 02: hills and optional mountain

Status: implemented; in-game scale and climbing review pending.
Based on the [relief design](../archetypes/landmass/hills-and-mountains.md).

## Placement and comparison

Keep [beach 01](01-small-beach-quadrant.md) as the flat control in
`[0,512) × [0,512)`. Add another landmass in the immediately eastern planning
quadrant, `[512,1024) × [0,512)`, centered at `(768,256)`. These adjacent
512-block experiment squares establish no new world limits or final creation layout.

The second island has the same coastline, shelf, sand, and base grass elevation
as the first. Its relief remains inside the grassy interior. There are 256
blocks of open water between their nearest dry shores.

Spawn is now `(682,2,170)`, on the second island's southwest beach, looking
northeast (yaw 45 degrees). Ground height is resolved with solid support.
Worlds too small to include it fall back to `(170,2,170)` if available,
then the original origin behavior for small fixtures.

## Independent options

The application reads `BW_RELIEF` once when creating a world:

| Launch | Eastern island |
| --- | --- |
| `./bwildering` or `env BW_RELIEF=both ./bwildering` | Hills and one mountain |
| `env BW_RELIEF=hills ./bwildering` | Grassy hills only |
| `env BW_RELIEF=mountain ./bwildering` | One mountain with its own foothills |
| `env BW_RELIEF=flat ./bwildering` | Flat control terrain |

The original island stays flat for all options. Restart to change the preset;
the choice belongs to the world and is used by sampling, materialization,
meshing, edit baselines, and cache reconstruction. It is not a live toggle that
would alter terrain beneath edits. Invalid option strings fail startup clearly.
Disk persistence is still not implemented.

## Shapes and composition

Descriptors below use the eastern quadrant's local coordinates; add 512 to X
to locate them in the world. Heights describe solid top surfaces.

| Feature | Local center X/Z | Radius X/Z | Rise above Y=3 |
| --- | --- | --- | --- |
| Main hill | 214 / 244 | 50 / 50 | 8 |
| Second hill | 286 / 306 | 45 / 45 | 6 |
| Mountain foothills | 276 / 238 | 75 / 75 | 10 |
| Mountain core | 276 / 238 | 68 / 40 | 30 in addition to its own foothills |

Each mound uses `(1 - r²)³` inside its elliptical influence, zero outside.
Hills combine by maximum. Mountain foothills and core form one mountain field;
combine that with hills by maximum. Total mountain rise is 40, producing a
maximum surface Y=43. The extended east-west core gives a gentler western
climbing route; north/south faces can have two-block rises. The wider core
differs intentionally from the narrow-core proposal to permit access with
existing jump physics.

Relief fades smoothly from island radius 100 to the grass boundary at 118.
Round the composed rise once to whole blocks. Hills keep grass over two dirt
blocks, then continuous stone. Mountain terrain exposes stone where composed
rise reaches 24 blocks or continuous surface slope exceeds 1.1. Lower slopes
retain grass and dirt. Dark stone and ocean rules remain unchanged.

Experimental descriptors are fixed and seed independent, matching beach 01.
Seeded placement, ridges, noise, snow, and additional islands are deferred.
The generator allocates no per-feature or per-sample memory.

## Review routes and evidence

- Start on the new beach and walk inland. Check the silhouette, flat space,
  hill height, and frequency of jumps at voxel rises.
- Main hill: approach its world center `(726,244)` from the west.
- Mountain: approach `(788,238)` from the west, starting near `(714,238)`.
  Compare foothills with and without the hill field.
- Hold view distance and direction fixed for performance comparisons.
  Relief adds vertical faces; distant terrain rendering remains separate.

`test_beach_generation` checks all four options, unchanged coastlines and flat
control, cap/foundation order, height/step bounds, eager/unloaded/resident mesh
agreement, edit survival after eviction, spawn stability and jumping, and actual
hill/summit access using existing movement. Headless and OpenGL suites cover
the application spawn and rendering. `terrain_capture` includes relief overhead,
spawn, hill, and mountain views.
