# WP-8 handoff — paper, README, DECISIONS, docs/notes (prose half)

Scope note: this handoff covers the *prose* half of WP-8 only.
The benchmark half is `docs/notes/allocator/HANDOFF-WP8-bench.md`, landed separately; this handoff consumes its numbers and does not repeat its methodology.

## Landed

- `papers/algorithms-for-trees.md` — new top-level section **"# Allocator awareness"**, inserted between "# Implementation experience" (and its "## Costs" subsection) and "# Non-goals".
  Seven subsections: "Edges carry the allocator" (D-A1 model, `Box`), "Layer types stay aggregates — a departure defended" (D-A2, the orthodoxy defense the plan calls for), "The cost, stated plainly" (the per-edge pointer cost), "Box-at-knot layers: the Option A refinement" (the WP-2 amendment: allocator-parameterized layers bound before `Fix`, allocator-carrying `fmap`, no verb overload), "The shared_ptr spine" (D-A5, `allocate_shared`), "The pmr surface" (D-A4), and "Measurements" (the benchmark numbers, with honest caveats — see below).
  One-sentence-per-line throughout; code excerpts transclude existing UUID anchors only (see "Interface decided").
  Self-reviewed with the `voice` skill against the paper's own "Costs"/"Design overview" register before finishing; one tightening edit made as a result (the per-edge-cost paragraph now lands on a short flat sentence instead of a run-on clause).
- `README.md` — new "## Allocators" section between the example list and "## Dependencies": what allocator-awareness means for a user, the zero-cost default-allocator claim, a four-line `pmr` usage sketch, and a link to the paper's new section.
- `docs/DECISIONS.md` — Decision 9 finalized: the one stale future-tense sentence ("the paper (WP-8) presents and defends it") now reads "the paper presents and defends it in its 'Allocator awareness' section (`papers/algorithms-for-trees.md`, D4322R0)".
  Read the rest of Decision 9 and its WP-2 amendment in full; nothing else was stale — no other edits made there, per the "finalize, don't rewrite" instruction.
- `docs/notes/allocator-awareness-plan.md` — Status line updated from "PLANNED — not yet started" to "COMPLETE", pointing at the handoffs, the deviations ledger, Decision 9, and the paper section.
  No other content touched.
- `docs/notes/allocator-adr.md` — Status line updated from "PROPOSED … awaiting review" to "RATIFIED", noting that the WP-2 hazard and its Option A resolution live in `DECISIONS.md`'s WP-2 amendment, not folded back into this document.
  No other content touched.
- `docs/notes/anchor-inventory.md` — added a new "## Allocator awareness (2026-07-18)" section cataloging the five `pmr.hpp` anchors WP-7 added (previously uncatalogued) and noting that four pre-existing anchors (`a1f81cb9` `Box`, `085bb189` `ExprF`, `a052ddcb` functor `Impl`/`Map`, `f6f4cee4` `BinaryTree`) grew allocator content in place and are now also consumed by the paper's new section.

## Interface decided

Nothing here is a library interface; this is prose consuming the surface WP-0…WP-7 shipped.
For downstream prose/docs work, the paper section's code excerpts transclude these existing anchors (no new anchors added — reused per the plan's stated preference):

- `box.hpp` — `a1f81cb9-ebb3-4872-9549-03ca140c61b2` (`Box` class).
- `expression.hpp` — `085bb189-a48e-4262-aefd-b64f8755e959` (`Add`/`Mul`/`ExprLayer`), `a052ddcb-f05b-41c9-85bf-4c443ab438b7` (`ExprFFmapAlloc`/`expr_fmap`).
- `binary_tree.hpp` — `f6f4cee4-9c55-4add-b38b-496936319294` (`BinaryTree::node`/`make_ptr`, the `allocate_shared` factories).
- `pmr.hpp` — `d91724ee-59a9-4e14-8681-2a140cef0266` (`allocator_type`/`Box` alias/`make_slot`).

The paper's own house-style HTML comment (mirroring the "Proposed API synopsis" section's existing convention) lists all five anchors with a "checked against 2026-07-18" note; re-diff before any revision that touches those headers.

## Verified

- No header, source, or build file touched — every change in this WP is `.md`.
  `make compile`/`make test`/`make compile-headers` were not re-run, per the gate's own carve-out ("if you touched only .md files, no build needed").
- `git diff --stat` confirms only the files listed under "Landed" changed, plus the pre-existing (not-mine) working-tree state from the benchmark half (`Makefile`, `benchmarks/beman/tree_algorithms/CMakeLists.txt`, `benchmarks/beman/tree_algorithms/allocator.bench.cpp`, `docs/notes/allocator/HANDOFF-WP8-bench.md`, `docs/notes/allocator/DEVIATIONS.md`) — nothing under `infra/`.
- Numbers cited in the paper's "Measurements" subsection trace to `docs/notes/allocator/HANDOFF-WP8-bench.md`'s "Verified" section (build 65,535 nodes, teardown 4,095 nodes, means as recorded there) for "run 1", **plus a second, quieter-machine re-run of `make bench` that the managing agent ran independently mid-task and reported directly in this session** ("run 2": default 47.2 μs / monotonic 17.9 μs / pool 93.8 μs teardown) — see Deviations, DEV-A09.
  The paper reports both runs as a range (monotonic teardown "roughly 2.6×–7.9× faster") rather than headlining run 1's single-run 7.9× figure, per the managing agent's explicit correction.
  Pool-slower-than-default and fold-noise-dominated are reported exactly as run 1's handoff found them; run 2 did not change those findings, only the teardown magnitude.

## Deviations

- **DEV-A09** — The brief said HANDOFF-WP8-bench.md's numbers are what the paper section must cite.
  Done instead: the paper's teardown table also cites a second benchmark run (a quieter-machine `make bench` re-run) that the managing agent reported directly in this session's transcript, not in any written handoff.
  Why: the managing agent flagged, mid-task, that run 1's headline 7.9× monotonic-teardown speedup is largely an artifact of that run's noisy default-allocator row (224 μs vs. 47 μs on the quieter machine), and asked for the range rather than the single number, with both runs' numbers available to cite.
  What it costs downstream: the paper's Measurements subsection now has one data point — the entire "run 2" column of its teardown table — with no corresponding entry in `docs/notes/allocator/HANDOFF-WP8-bench.md` or any other written record; a reader who goes looking for where "47.2 μs" came from will not find it in the docs tree, only in this handoff's "Verified" section above.
  Recommend the managing agent either append run 2 as an addendum to `HANDOFF-WP8-bench.md` (its file, not touched here) or accept this handoff's "Verified" section as the record.

## Open questions / warnings for downstream

- **The teardown magnitude is confirmed unstable across runs; treat "2.6×–7.9×" as the citable claim, not either endpoint alone.** A third run would not be surprising if it landed outside that range too — the mechanism (monotonic `deallocate` is a no-op) is robust; the number is not. Any future revision of this paper that tightens the range should re-run on a genuinely idle machine, several times, before narrowing it.
- **`docs/notes/` cleanup was scoped to the allocator campaign's own notes.** `punch-list.md`, `extraction-plan.md`, `article-reconciliation-memo.md`, `engineering-reality-sections.md`, and `representations-and-sequence-interfaces.md` predate the allocator-awareness campaign (release-gate and representation work from 2026-07-05..17) and were left untouched — they are a different work stream's notes, not "the campaign['s]... working notes" the WP-8 brief meant. If the managing agent wants a broader `docs/notes/` sweep across all pre-allocator notes, that is a separate task, not folded into this one by judgment call.
- **`docs/notes/allocator/DEVIATIONS.md` and every `HANDOFF-WP*.md` file were read but not edited**, per the plan's instruction that the managing agent owns the ledger and the handoffs are the audit trail.
- **The paper section's placement is a judgment call, not a plan mandate.** The plan says only "ADD a section"; this handoff put it as a new top-level section between "Implementation experience" and "Non-goals" because it needed room for both design defense and measured cost, matching the weight of "Folding and traversing trees" elsewhere in the paper. If the managing agent's voice review prefers it folded into "Design overview" or "Implementation experience" instead, that is a placement edit, not a content rewrite.
