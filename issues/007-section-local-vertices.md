# Measure vertex costs and evaluate section-local coordinates

Priority: P2 — measurement-gated

Status: Open

Source: [Code review, 26 October 2026](../review.md) — Review §2: vertex representation.

## Problem

`section_mesh_build()` emits absolute world-space float positions. Section-local positions could enable smaller vertex formats and improve precision far from the origin, but the review recommends measuring vertex memory and GPU bandwidth first.

## Work

- Measure vertex memory and GPU bandwidth for representative terrain.
- Evaluate local section coordinates with a per-section rendering transform.
- Assess distant-coordinate precision and the potential for a compact vertex format.
- Adopt a change only if measured costs or demonstrated precision problems justify it.

## Acceptance criteria

- [ ] Measurements and the implementation/defer decision are recorded.
- [ ] If adopted, rendering preserves section placement, seams, block scale and world origin semantics.
- [ ] If adopted, culling and rendering agree at negative and distant coordinates.
- [ ] Before/after memory, performance and precision results are available.

## Order and dependencies

After streaming correctness; coordinate with renderer audit 010.
