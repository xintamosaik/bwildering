# Validate renderer integration omitted from the review snapshot

Priority: P1 — proposed verification priority

Status: Open

Source: [Code review, 26 October 2026](../review.md) — Review §6: review limitations.

## Problem

The reviewed snapshot omitted `main.c`, so renderer synchronization, OpenGL resource ownership, frustum culling and frame-time budgets were not independently assessed. The reviewer also did not compile or run the tests. This is a verification gap, not evidence of a renderer defect.

## Work

- Review the available renderer implementation and its interaction with chunk residency.
- Compile and run the relevant world and renderer tests, documenting environment limitations.
- Verify mesh replacement/retirement and synchronization with dirty or evicted sections.
- Exercise frustum culling and frame-time budgeting while moving through the world.

## Acceptance criteria

- [ ] Relevant build/test commands and results are recorded, including unavailable runtime checks.
- [ ] GPU resources have explicit ownership and retirement paths, with failures preserving valid state.
- [ ] Culling and streaming transitions do not leave missing or stale geometry in exercised scenarios.
- [ ] Movement measurements identify mesh upload/retirement costs and frame-time spikes.
- [ ] Confirmed defects are fixed or tracked as concrete follow-up issues.

## Order and dependencies

Alongside 001 and 002; use the full repository rather than assuming the snapshot was complete.
