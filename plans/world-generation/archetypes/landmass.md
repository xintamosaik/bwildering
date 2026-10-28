# Archetype: primary landmass

Status: first profile implemented as experimental beach 01. One instance belongs to every
[creation](../README.md).

## Purpose

Provide the creation's main continuous area of land and its safe waking location.
It is distinct from the smaller islands reserved for archipelago areas. Its
first profile is [round, flat land with beach and grass](landmass/round-flat-beach-grass.md).

## Shared contract

- Form one connected main body with solid material continuously down to the
  seabed. Avoid floating ground, disconnected fragments, and accidental islands.
- Replace ocean water inside the solid land volume. Retain water outside it and
  the dark-stone foundation underneath it.
- Give the coastline a shape derived from the landmass description, rather than
  chunk edges or the edges of a planning square.
- Resolve ground height before choosing beach, soil, and grass materials.
- Provide dry, accessible ground for eventual spawn selection. The first
  experimental beach spawn was `(170, 2, 170)`; relief experiment 02 moves it to
  `(682, 2, 170)`. Elevation comes from solid support after terrain sampling.
- Produce the same blocks through direct sampling and chunk materialization,
  including at signed coordinates and across region borders.

## Profiles

The round, mostly flat beach-and-grass profile is implemented. The next proposed
extension is [separate hills and optional mountains](landmass/hills-and-mountains.md),
with mountains providing their own foothill base. It is implemented in
[experimental quadrant 02](../experiments/02-hills-and-mountain-quadrant.md).
Cliffs, lakes, rivers, caves, vegetation, structures, and other landmass profiles
remain deferred. A generalized biome system is not needed for this extension.

## Review criteria

The main landmass reads as one place, has a recognizable coast, offers a clear
beach-to-grass progression, and lets the player wake and walk on dry land.
Dimensions must be judged through actual routes as well as overhead images;
being connected in a height map is not enough if the route is impractical.
