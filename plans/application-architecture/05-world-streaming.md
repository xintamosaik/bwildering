# Stage 5: separate CPU residency policy from GPU synchronization

Status: planned. Order: 5 of 5. Prerequisite: [stage 4](04-tick-command-timing.md).

[Architecture overview and shared verification](../application-architecture.md).

## Problem description

Renderer synchronization currently chooses CPU residency, materializes chunks
and coordinates their eviction as well as managing meshes and GPU resources.
This couples CPU cache policy to one player's visual distance. A headless server
would need CPU residency without OpenGL, while multiple clients would eventually
need visual caches independent of authoritative simulation interest.

## Implementation

1. Move desired-residency calculation and CPU materialization scheduling to a
   small world-streaming module. Application supplies the current local region
   of interest; the renderer consumes resulting world data and mesh work.
   Preserve the single-player radius and residency bounds for now.
2. Keep one coordinated work budget across materialization, meshing and upload;
   splitting modules must not grant each a fresh full-frame allowance. Preserve
   nearest/edit priority, aging, bounded admission and synchronous drain support.
3. Make retirement order explicit: retire departing GPU resources, cancel tasks
   referring to departing identities, then recycle CPU caches. Preserve residency
   progress when renderer metadata allocation fails. Track tasks by coordinates
   and revision, never borrowed section pointers or unstable array indices.
4. Keep mesh dirty/revision handling behind a documented cache-sync contract.
   Do not introduce replicated dirty flags or a world-change message log yet.
5. Continue using view distance to request the local visual cache, but express
   that as application policy. The streaming module must not require a renderer
   or GL context. Its region input can later be computed from server simulation
   interest independently of client visual distance.

## Acceptance criteria

- CPU region calculation, materialization and eviction can be exercised without a renderer or GL context.
- One coordinated allowance still bounds newly admitted materialization, mesh and upload work; module separation does not multiply the budget.
- Retirement and cancellation precede CPU recycling; no queued work retains borrowed section pointers or unstable indices.
- Existing exhaustion, stale-revision, failure/retry, edit persistence and convergence fixtures pass, including renderer metadata failure during residency progress.
- Final meshes/material ranges match synchronous references and valid old geometry remains drawable during retries.
- Comparable serial captures document residency, operation counts and any timing changes; unrelated scheduler tuning is deferred.
- Application supplies the current local region policy; multi-player interest and replication remain deferred.

## Validation and completion evidence

Apply the [shared verification requirements](../application-architecture.md#verification-for-implementation-stages), including builds, affected tests and
Clang-Tidy checks. Record commands, results, limitations and any intentional
behavior changes here when implementing this stage.

Implementation has not started; no acceptance criterion is claimed as verified.
