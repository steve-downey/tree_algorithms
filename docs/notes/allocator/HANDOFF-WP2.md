# WP-2 handoff — `child_slot` / `make_slot` / `Fix` plumbing

## Landed
- `include/beman/tree_algorithms/child_slot.hpp` — `child_slot<A, Allocator = std::allocator<A>>` and `child_slot_t<A, Allocator = std::allocator<A>>`; knot specialization rebinds the allocator to `Fix<F>`; allocator-tagged `make_slot` overload (Box at the knot via tagged `make_box`, uses-allocator-constructed element on the inline optional branch); default `make_slot` constrained with `!leads_with_allocator_arg`.
- `tests/beman/tree_algorithms/child_slot.test.cpp` — additivity static_asserts, constexpr tagged-inline static_assert, runtime pmr propagation into both knot Box and inline allocator-aware element.
- `fix.hpp` — **unchanged** (see DEV-A03).

## Interface decided
- `child_slot_t<A, Allocator = std::allocator<A>>`: one-arg spelling unchanged; knot → `Box<Fix<F>, rebind_alloc<Fix<F>>>`; complete types → `std::optional<A>`.
- `make_slot<A>(std::allocator_arg, alloc, args...)` → `child_slot_t<A, Allocator>`. Knot: `make_box<A>(allocator_arg, alloc, args...)`. Inline: `optional<A>(in_place, make_obj_using_allocator<A>(rebound_alloc, args...))`. **This is the tool WP-3/4/5 use to build allocator-carrying children.**
- Reuse `leads_with_allocator_arg` from `box.hpp` — do not re-roll.

## Verified
- `make compile` clean, `make test` = **82/82**, `make compile-headers` green (child_slot self-contained).
- `ctest -R child_slot` = 5/5. Runtime test: knot Box's `get_allocator().resource()` and an inline `pmr::vector`'s `get_allocator().resource()` are both the supplied `monotonic_buffer_resource`.
- Constexpr tagged-inline `make_slot<int>` = 8 asserted; one-arg `child_slot_t`/`make_slot` bit-identical to before.
- All pre-existing tests pass unmodified.

## Deviations
- **DEV-A03 — `wrap_fix` gets NO allocator overload (plan listed one in WP-2 scope).** Rationale: `Fix<F>` is not allocator-aware (D-A2), and the layer handed to `wrap_fix` already carries its `Box` children's allocators — they were built by the tagged `make_slot`. `wrap_fix(rvalue)` moves those boxes in (allocator preserved); `wrap_fix(const&)` deep-copies, each `Box` copy using its own stored allocator via SOCCC. There is no construction site inside `wrap_fix` that needs an allocator. (A hypothetical "copy this tree onto a *different* allocator" `wrap_fix` would have to rebuild every box through `fmap`, which is the verb-materialization question below, not a `wrap_fix` overload.) `unwrap_fix` needs nothing, as the plan already expected. Within WP-2's own file, deviate-then-report; recorded here.

## Open questions / warnings for downstream — STOP-THE-LINE (escalated to Steve)

**A load-bearing conflict between D-A2 and D-A1/D-A3 for the Box-at-knot layers.**
Proven by spike `scratchpad/wp2_fmap_spike.cpp` (compile error, captured):

The fixed-arity layer aggregates (`ExprF`'s `Add`/`Mul`, `BinaryTreeF`, `NatF`) declare each recursive child as `child_slot_t<A>` — which, with the **defaulted** allocator, is exactly `Box<Fix<F>, std::allocator<Fix<F>>>`. **The knot Box's allocator type is baked into the layer's field type, and it is `std::allocator`.** A stateful `std::pmr::polymorphic_allocator` produces a *different* `Box` type (`Box<Fix, polymorphic_allocator<Fix>>`) that does not fit that field:

```
error: could not convert Box<…, polymorphic_allocator<Fix<ListF>>>
        to Box<…, std::allocator<Fix<ListF>>>
```

So there is **no construction site — not fmap, not wrap_fix, not a smart constructor — that can put a stateful allocator into a Box-at-knot layer**, because the field type forbids it. The allocator-carrying-fmap idea I sketched above is *insufficient on its own*: it changes where boxes are built, but the field they must be stored in is still `std::allocator`-typed.

Scope of the conflict — it is specific, not universal:
- **Hits**: Fix-over-fixed-arity-Box layers — `ExprF`, `NatF`, and `BinaryTreeF` *in its Fix form*. These need a decision to support stateful (pmr) allocators.
- **Does NOT hit**:
  - **Rose tree (WP-4)** — children are `std::vector<A>`; the pmr rose is a *distinct* layer with `std::pmr::vector<A>` (D-A4). `polymorphic_allocator` is type-erased over the resource, so one vector type carries any resource at runtime. `RoseLayer<T>` already binds before `Fix`, so a pmr rose layer is clean.
  - **`shared_ptr`-spine representations (WP-5/6)** — `allocate_shared` stores the allocator in the control block; the node type is unchanged (D-A5).
  - **The entire stateless default path (WP-1, WP-2)** — additive and correct; nothing here regresses.

Options for the Box-at-knot pmr story (Steve to choose):
- **(A) Allocator-parameterize the Box-at-knot layers** — `Add<A, Allocator = std::allocator<A>>` etc., binding the allocator before `Fix` the way `RoseLayer<T>` binds `T` (`Expr = Fix<ExprLayer<Alloc>::F>`). Most faithful to "one parameterized `child_slot`", but ripples through every layer spelling, the `Fix` aliases, and the paper — in tension with D-A2's "layer types stay simple aggregates".
- **(B) Pmr-twin layer types** — distinct `pmr::ExprF` etc. whose child_slot uses `polymorphic_allocator`, exactly as D-A4 already does for rose. Localized, but duplicates each Box-at-knot representation.
- **(C) Type-erase the knot Box's allocator** — make the knot Box always hold a `polymorphic_allocator`/`memory_resource*`, so one Box type carries any resource. Uniform, but gives the *default* path a per-edge pointer and a virtual call — violates D-A1's "zero bytes / bit-for-bit for `std::allocator`" unless the default path stays a separate type (which is option B again).
- **(D) Scope pmr to the representations that support it cleanly** — ship pmr for the rose tree and the `shared_ptr`-spine trees (and `pmr::` aliases/factories for those), and document the Box-at-knot Fix trees as default-allocator-only for now. Smallest, honest, but narrows WP-7's "every representation" claim.

Because this touches §3 (D-A1/D-A2/D-A3/D-A6) and `DECISIONS.md` (Decision 9), per plan §6.5 the line stops here. **WP-3…WP-7 are not dispatched** until Steve picks a direction and the ADR/plan/Decision 9 are amended. WP-1 and WP-2 (stateless additive path) stand and are green.
