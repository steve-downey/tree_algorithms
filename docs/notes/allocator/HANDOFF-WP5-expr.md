# WP-5 (expression tree) handoff — Option A rollout, canonical representation

This is the first production representation converted under the DEV-A04 / Option A design.
It proves the pattern (including the one open unknown — `functor_typeclass` registration on a parameterized layer) and is the template the remaining Box-at-knot representations follow.

## Landed
- `include/beman/tree_algorithms/expression.hpp`
  - `Add<A, Allocator = expr_default_allocator>`, `Mul<A, Allocator = …>`; `Const<A>` unchanged (childless).
  - `expr_default_allocator = std::allocator<std::byte>` — a **fixed** (value-type-independent) default so `Add<A>` names the exact type the default `ExprF` variant holds.
  - `ExprLayer<Allocator>` binder; `ExprF<A> = ExprLayer<expr_default_allocator>::F<A>`; `Expr = Fix<ExprF>` — all unchanged spellings, unchanged types.
  - `functor_typeclass` and `layer_fold_typeclass` registered generically on the variant shape `variant<Const<A>, Add<A,Alloc>, Mul<A,Alloc>>` (covers default and pmr).
  - `expr_fmap(alloc)` → `ExprFFmapAlloc<Allocator>`: the allocator-carrying fmap.
- `tests/beman/tree_algorithms/expression.test.cpp` — all 6 prior cases unchanged and green; new `PmrUnfoldBuildsEntirelyFromThePool` (null-upstream pool).

## The reusable pattern (for BinaryTreeF, NatF — any Box-at-knot layer)
1. Add `typename Allocator = <fixed default, e.g. std::allocator<std::byte>>` to each alternative that holds `child_slot_t` children; store `child_slot_t<A, Allocator>`. Leave childless alternatives alone.
2. Add a `XxxLayer<Allocator>::template F<A>` binder; keep the old unary alias as the `<fixed default>` instantiation; keep the `Fix<...>` alias.
3. Register `functor_typeclass` / `layer_fold_typeclass` on the **variant/struct shape** with `Allocator` deduced, not on the old alias.
4. The functor instance's fmap uses the **plain** `make_slot` (correct for default-alloc unfold and for every fold; a pmr *unfold* via lookup simply won't instantiate at the knot — documented).
5. Provide `xxx_fmap(alloc)` — same shape, tagged `make_slot<B>(allocator_arg, alloc, …)` — for pmr unfold with the explicit-fmap verbs.

## Interface decided
- `expr_fmap(alloc)` is the allocator-aware build path: `unfold_fix<ExprLayer<PA>::template F>(coalg, expr_fmap(a), seed)`.
- pmr expression tree type: `Fix<ExprLayer<std::pmr::polymorphic_allocator<std::byte>>::template F>` — WP-7 will alias this as `pmr::Expr` and wrap the coalgebra/smart-ctor ergonomics.
- **Verbs unchanged**: no allocator overload on `unfold_fix`/`fold_fix`/`refold`. Confirmed.

## Verified
- `make compile` clean, `make test` = **83/83**, `make compile-headers` green.
- New test: a pmr expression tree unfolds over a `monotonic_buffer_resource` with `null_memory_resource` upstream (would throw on any leak); root knot Boxes' `get_allocator().resource()` is the pool; lookup-fold sums the constants to 28.
- All prior expression tests pass unmodified.

## Open questions / warnings for downstream
- Lookup-based *unfold* of a pmr tree won't compile (plain `make_slot` at the knot yields the std::allocator Box type). This is by design; pmr unfold uses `expr_fmap` + the explicit-fmap verb. WP-7/WP-8 should state it.
- Still to convert under Option A: `NatF` and any Box-at-knot layers in the test suite are local to tests (unaffected). Production Box-at-knot representations remaining: none besides expression in `include/` — `BinaryTreeF` in `binary_tree.hpp` is a `shared_ptr`-spine adaptor (WP-5 D-A5 path, allocate_shared, different mechanism), and the rose tree is vector-children (WP-4, distinct pmr::vector layer, different mechanism).
