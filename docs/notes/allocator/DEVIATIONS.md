# Allocator campaign — deviations ledger

Roll-up of every departure from `docs/notes/allocator-awareness-plan.md` §3 / WP scope / gates.
Numbered sequentially across the whole campaign; the managing agent owns the sequence.
Shape mirrors the fixpoint repo's `ops/DEVIATIONS.md`.

| ID | WP | Plan said | Done instead | Why | Status |
|----|----|-----------|--------------|-----|--------|
| DEV-A01 | WP-0 | Spike "compiles under `make compile`" | Compiled standalone with the repo's exact flags | WP-0 scope forbids putting throwaway code in the build tree; same toolchain/flags | Accepted |
| DEV-A02 | WP-1 | File list names "module interface" as a touch | No `.cppm`/`.hpp` edit | `box.hpp` is already in the umbrella the module exports; no new header added | Accepted (non-departure) |
| DEV-A03 | WP-2 | "wrap_fix allocator-extended overload" | `wrap_fix` left unchanged | `Fix` isn't allocator-aware; the layer's Box children already carry their allocators from tagged `make_slot`; `wrap_fix` only moves/copies | Accepted; superseded by the DEV-A04 escalation |
| DEV-A04 | WP-2 | D-A2: layer aggregates use the defaulted `child_slot_t<A>`; D-A6: verbs take a trailing allocator | Spike proved a stateful allocator cannot live in a fixed layer field. **Resolved by Steve (2026-07-18): Option A** — parameterize the Box-at-knot layers on the allocator, bind before `Fix`; verbs carry the allocator in an allocator-carrying `fmap` (no verb overload). Amends D-A2, reinterprets D-A6; recorded in `DECISIONS.md` (2026-07-18 WP-2 amendment) | Resolved; line resumed |

## Notes

- DEV-A04 was validated end-to-end by `scratchpad/wp_optionA_spike.cpp` before resuming: default `std::allocator` path constexpr and bit-unchanged; a 100-node pmr list unfolds from a null-upstream pool with zero leak, via the *existing* `unfold_fix`.
- The stateless default path (WP-1, WP-2) never needed this and stands green throughout.
