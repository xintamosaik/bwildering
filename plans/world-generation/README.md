# Creation blueprint and terrain archetypes

Status: blueprint design with experimental beach 01 implemented. The complete
four-area layout and small islands remain planned. See the
[masterplan](../world-generation-masterplan.md).

The first scoped implementation is the
[experimental 512-block beach quadrant](experiments/01-small-beach-quadrant.md).
Its terrain and beach-spawn follow-up are implemented. The player starts at
`(170, 2, 170)`; the accepted size and placement do not establish world limits.

## Composition of every creation

Here, a creation means one assembled terrain blueprint. It describes broad
geography, not a voxel chunk, a save-file region, or a zoom level. Whether a
future world consists of one creation or several repeating creations is open.

| Area | Instances per creation | Intended content | Next implementation scope |
| --- | --- | --- | --- |
| [Primary landmass](archetypes/landmass.md) | 1 | One connected main landmass | Round, mostly flat, with beach and grass |
| [Ocean](archetypes/ocean.md) | 1 | Broad open water | Keep the existing foundation, seabed, and water |
| [Archipelago](archetypes/archipelago.md) | 2 | Each area eventually contains 2–3 small islands | Reserve the areas; leave them as water |

These are four planning areas using three archetypes. The two archipelago areas
eventually contribute 4–6 small islands altogether, in addition to the main
landmass. That count does not apply to the next implementation: it adds only
the primary landmass.

Area counts do not imply equal square sizes, equal land coverage, or equal
travel times. One ocean area also does not mean that water stops at its boundary:
water around the main landmass and inside the archipelago areas belongs to the
same connected ocean wherever the geography permits it.

## Current decisions

- Preserve this composition for every creation; vary placement and shape through
  the seed without changing the required counts.
- Develop the [round, flat beach-and-grass profile](archetypes/landmass/round-flat-beach-grass.md)
  first. Mountains, other landmass profiles, and small-island generation wait.
- Eventually wake the player on dry, safe solid ground on the first landmass.
  The beach-spawn follow-up now selects `(170, 2, 170)` using solid support,
  rather than the occupied water surface.
- Preserve dark stone, the stone seabed, and static ocean water. Build land
  upward from the seabed and replace water inside its volume.
- Planning boundaries organize descriptions. Coastlines may cross them and must
  remain continuous when chunks load in different orders.

## Description shared by archetypes

A future description needs a stable identity, archetype/profile, seed, horizontal
anchor, extent of influence, and profile parameters. The creation owns the four
area descriptions; the landmass profile derives the actual coastline, ground
height, and surface layers. These are conceptual responsibilities, not a proposed
C struct layout or a new persistence format.

Keep plan sampling deterministic and available without generating every chunk.
Saved chunk snapshots will continue to take precedence over regeneration once
disk persistence exists. Archetype/profile versions and their parameters must
be accounted for in the future generator-version policy.

## Order for the next experiment

1. Use the experimental positive quadrant for the first main landmass; retain the
   four-area composition as the larger blueprint.
2. Generate the existing foundation, seabed, and ocean as the baseline.
3. Shape only the main landmass using its round, flat profile.
4. Apply sand, dirt, and grass after resolving the solid ground shape.
5. Finish the terrain experiment with spawn unchanged.
6. Move spawn onto the first land in a separate follow-up, using solid support.
7. Review shape, beach width, scale, and walking feel in-game before adding islands.

The experimental terrain and spawn follow-up are implemented. Initial size and
placement feedback is accepted; the wider four-area system remains a design.

## Still open

Final area dimensions, spatial arrangement, overlap rules between future creations,
exact spawn preference (grass or dry sand), and the final beach/grass profile
values need experiments or further discussion. A 2×2 arrangement could be tested,
but is not a required layout. No fixed quadrant ordering or equal-size partition
is established here.

Keep the predominantly ocean direction from the masterplan. Measure water
coverage after choosing sizes; the four-area count alone cannot establish it.
