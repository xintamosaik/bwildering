# Archetype: archipelago area

Status: reserved for later. Do not implement small islands in the next landmass
experiment. See the [creation blueprint](../README.md).

## Purpose and count

Every creation includes two independent instances of this archetype, referred to
as archipelago A and archipelago B. Eventually, each instance contains 2–3 small
islands: 4–6 small islands across both areas, separate from the primary landmass.

The requirement is for two areas capable of holding those islands, rather than
two individual islands or 2–3 islands shared between both areas. Instance count
is fixed by the creation; shape, spacing, and the later per-area island count
can depend on the seed.

## Behavior while deferred

Keep the two area identities and their intended roles in the design. Their
terrain remains the existing ocean baseline when the first main landmass is
implemented. Do not generate island centers, raise seabed peaks into land, borrow
pieces of the main landmass, or add temporary terrain just to fill these areas.

No region metadata or placeholder generator code needs to be implemented merely
because these documents exist. The next implementation can introduce only the
planning information actually needed for the main landmass.

## Future contract

Later work should define island profiles, sizes, shape variation, spacing, and
water passages. Islands should be solid extensions of the seabed, and their
placement must remain independent of exploration order. Leave enough water for
the group to read as separate islands rather than an accidental second main
landmass. Shared beaches or narrow passages are future design choices.

No island style is selected yet. In particular, the round beach-and-grass main
landmass profile does not automatically become the small-island profile.
