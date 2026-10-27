# Archetype: ocean

Status: the flat ocean baseline exists; its role in the four-area
[creation blueprint](../README.md) is documentation only.

## Purpose and count

One ocean area belongs to every creation. It provides a substantial stretch of
open water between destinations. Water also continues around the main landmass
and through the two reserved archipelago areas; this archetype's count does not
split those waters into separate sealed basins.

The ocean area remains free of deliberate land generation in the next experiment.
The exact area dimensions and placement are open. Preserve the wider masterplan's
ocean-dominated direction; counting one ocean area out of four does not imply
that water should occupy only a quarter of the world.

## Existing baseline to retain

- Dark stone at `-128 <= y < -64`.
- Stone at `-64 <= y < -32`.
- Water at `-32 <= y <= 0` where land has not replaced it.
- Air above the water; the water's top face is at voxel height `1`.

Those values are current generator behavior, not new changes in this document.
Keep water connected across chunks and planning boundaries. The main landmass's
coastal profile owns its local shelf and approach to the beach; ocean boundaries
must not cut that shelf into a vertical wall.

## Deferred work and review

Seabed noise, trenches, reefs, underwater biomes, currents, tides, flowing water,
boats, and decorations are deferred. Use the existing shader and swimming while
reviewing scale. Check that open water feels substantial, the coastline meets it
continuously, and unloaded/resident queries agree on the water volume.
