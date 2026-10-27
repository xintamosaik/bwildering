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
  experimental beach spawn is `(170, 2, 170)`, with elevation resolved from
  solid support after the terrain was finished.
- Produce the same blocks through direct sampling and chunk materialization,
  including at signed coordinates and across region borders.

## Profiles

Only the round, mostly flat beach-and-grass profile is specified now. Hills,
mountains, cliffs, lakes, rivers, caves, vegetation, structures, and other
landmass profiles are deferred. Do not introduce a generalized biome system
or random profile selection merely to implement this first case.

## Review criteria

The main landmass reads as one place, has a recognizable coast, offers a clear
beach-to-grass progression, and lets the player wake and walk on dry land.
Dimensions must be judged through actual routes as well as overhead images;
being connected in a height map is not enough if the route is impractical.
