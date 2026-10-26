# Represent empty, uniform and dense voxel sections explicitly

Priority: P1 concern — implement after streaming

Status: Complete

Source: [Code review, 26 October 2026](../review.md) — Review §2: terrain storage; §5 item 4.

## Problem

Every allocated subchunk carries a 4,096-byte block array, even when all blocks are identical. Homogeneous underground sections therefore consume avoidable memory.

## Work

- Introduce explicit empty, uniform-material and dense storage states.
- Preserve dense storage for mixed sections.
- Define transactional transitions when an edit changes a uniform section into a dense section.
- Adapt block lookup, voxel counts, materialization and meshing to the representation.
- Measure memory savings on representative mountainous terrain, including metadata and allocation overhead.

## Acceptance criteria

- [x] Uniform sections store a material without a full block array.
- [x] Lookups and meshes match the existing representation for equivalent worlds.
- [x] Edits across storage states preserve block values and metadata.
- [x] Allocation failure during expansion leaves the original section unchanged.
- [x] Memory measurements demonstrate savings and report any performance tradeoff.

## Order and dependencies

After reliable streaming and lifecycle tests (001–002). Consider findings from 005.

## Findings from issue 005

[Mesh-query measurements](../mesh-query-performance.md) support build-local
neighbor and column reuse. This reduces meshing to seven section lookups per
build, so direct indexing is deferred. Preserve that reuse when adapting reads
to uniform storage; arbitrary-elevation edits still use the sparse section list.

## Implementation and validation

See [storage design and measurements](../section-storage.md) for transactional
transitions, exact dense-reference comparisons, allocation-failure coverage and
allocator-inclusive mountainous terrain results (70–81% savings).
