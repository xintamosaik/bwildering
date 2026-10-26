# Separate edit storage from resident terrain

Priority: P2

Status: Open

Follow-up to [001](001-bounded-chunk-streaming.md).

Edited procedural chunks currently retain all materialized CPU sections until
world destruction, including chunks whose edits were subsequently undone.
Exploration without edits is bounded; editing arbitrarily many chunks is not.
Edits have no save/load support and are lost on world destruction.

Introduce a sparse edit overlay or persistent edit store before allowing edited
CPU chunks to evict. Define edit capacity, failure behaviour and save/load
semantics. Reapply modifications over the deterministic seed baseline on
materialization, including removals and edits outside generated elevations.
Test allocation/I/O failures and materialize → edit → retire → reload → revisit.
