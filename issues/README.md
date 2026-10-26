# Review issues

Actionable work extracted from [review.md](../review.md), dated 26 October 2026. Each issue has its own file, scope and acceptance criteria. Individual issue status and evidence record subsequent implementation and verification.

Priorities marked “proposed” are triage suggestions for findings that the review did not explicitly rank. Suspected correctness problems remain investigations. Related feedback is consolidated to avoid duplicate work.

| ID | Issue | Priority |
| --- | --- | --- |
| 001 | [Finish bounded chunk residency and coordinated eviction](001-bounded-chunk-streaming.md) — complete | P1 |
| 002 | [Test world-state invariants across chunk lifecycle transitions](002-lifecycle-invariant-tests.md) — complete | P1 |
| 003 | [Investigate surface-height consistency before and after materialization](003-surface-height-consistency.md) | P1 — proposed correctness investigation |
| 004 | [Verify hidden sections become mesh candidates after neighboring edits](004-mesh-candidate-correctness.md) | P1 — proposed correctness investigation |
| 005 | [Profile mesh neighbor queries and remove measured repeated work](005-mesh-neighbor-query-performance.md) — complete | P2 |
| 006 | [Represent empty, uniform and dense voxel sections explicitly](006-uniform-section-storage.md) — complete | P1 concern — implement after streaming |
| 007 | [Measure vertex costs and evaluate section-local coordinates](007-section-local-vertices.md) | P2 — measurement-gated |
| 008 | [Review repeated validation in performance-critical internal paths](008-validation-boundaries.md) | P2 — proposed follow-up |
| 009 | [Evaluate greedy meshing after establishing streaming and memory baselines](009-evaluate-greedy-meshing.md) | P3 — deferred evaluation |
| 010 | [Validate renderer integration omitted from the review snapshot](010-renderer-lifecycle-audit.md) | P1 — proposed verification priority |
| 011 | [Separate edit storage from resident terrain](011-persistent-edit-storage.md) | P2 |

## Suggested sequence

1. Establish streaming behaviour and renderer evidence (001, 010), with lifecycle and targeted correctness tests (002–004).
2. Profile neighbor queries and repeated terrain sampling (005).
3. Introduce uniform storage once streaming is reliable (006).
4. Use measurements to decide on local vertices and validation changes (007–008).
5. Evaluate greedy meshing after those foundations (009).

The review's positive assessments and advice against premature architectural abstractions are context, rather than separate implementation issues.
