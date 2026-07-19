# WP-0 handoff — Design spike and ADR

## Landed
- `docs/notes/allocator-adr.md` — the ADR: validated D-A1…D-A6, the spelling decision, the downstream interface, and the draft Decision 9.
- `docs/notes/allocator/HANDOFF-WP0.md` — this note.
- (not landed) `scratchpad/wp0_spike.cpp` — throwaway spike, kept for review; must not enter the build tree.

No production code changed (WP-0 scope).

## Interface decided
- `Box<A, Allocator = std::allocator<A>>`: `allocator_type`, `[[no_unique_address]]` allocator, `allocator_traits` allocate/construct/destroy/deallocate, element via `std::make_obj_using_allocator`, SOCCC on copy / steal on move / POCCA+POCMA on assign with unequal element-wise fallback, `get_allocator()`.
- `child_slot<A, Allocator = std::allocator<A>>` and `child_slot_t<A, Allocator = std::allocator<A>>`: inline `std::optional<A>` at complete types; `Box<Fix<F>, rebind_alloc<Fix<F>>>` at the knot.
- **Spelling (binding):** variadic factories (`make_box`, `make_slot`, smart constructors) take `std::allocator_arg`-tag-first; their un-tagged default overloads are constrained with `!leads_with_alloc_arg<Args...>()` (WP-1 gives the helper its final name/home). Fixed-arity verbs (`unfold_fix`, `refold`, `to_fix`, `from_fix`) take a trailing allocator parameter.
- Layer types stay aggregates; no `std::uses_allocator` specialization for `Fix` or layers.

## Verified
- Spike compiles clean (`-Wall -Wextra -std=gnu++23 -fconstexpr-ops-limit=1000000000 -O2`, gcc 15.2) and runs.
- `static_assert`s pass at compile time: constexpr Box round-trip (== 42), `sizeof(Box<int>) == sizeof(int*)`, aggregate layer literal compiles, allocator-tagged `make_slot` inline branch constexpr (== 8).
- Runtime run: `box_roundtrip = 42`, `nested_propagation = true (resource threaded)` — a `pmr::vector` inside `Box` allocated from the supplied `monotonic_buffer_resource`.
- Baseline `make compile` was green before any change; no build-tree files were touched, so it remains green.

## Deviations
- **DEV-A01**: The plan (WP-0 gate) says "spike compiles under `make compile`." The spike was compiled *standalone with the repository's exact flags* instead, because WP-0's own scope forbids landing production code / entering the build tree. Same toolchain, same flags; intent (it compiles under our toolchain) satisfied. Frees nothing / breaks nothing downstream.
- Spelling refinement (not a departure — the plan delegated (e) to the spike): the default variadic factory overloads **must** be constrained to reject a leading `allocator_arg_t`. Recorded because it is load-bearing for WP-1/WP-2, not because it contradicts the plan.

## Open questions / warnings for downstream
- WP-1 owns the final name and header home of the `leads_with_alloc_arg` / `not_allocator_tagged` helper; every variadic factory (WP-2, WP-4, WP-5) must reuse it, not re-roll it.
- Decision 9 is DRAFT. Per the WP-0 gate it must land in `DECISIONS.md` (after review) **before WP-1 is dispatched**. WP-8 finalizes it.
- The unequal-allocator move-assignment fallback in `Box` needs a POC-trait-matrix test in WP-1 (all four copy/move × propagate/don't cases) — the spike exercised only the default-allocator happy path.
