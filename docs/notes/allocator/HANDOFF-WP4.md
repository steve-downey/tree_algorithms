# WP-4 handoff — Rose tree

## Landed

- `include/beman/tree_algorithms/rose_tree.hpp`
  - `rose_default_allocator = std::allocator<std::byte>` — a fixed
    (value-type-independent) default, mirroring `expr_default_allocator`.
  - `rose_children_t<A, Allocator>` — alias for
    `std::vector<A, rebind_alloc<A>>`, the children-vector type shared by
    `RoseF`, the functor instance, and `rose_fmap`.
  - `RoseF<T, A, Allocator = rose_default_allocator>` — gained the
    `Allocator` parameter; `children` is now `rose_children_t<A,
    Allocator>` instead of a hardwired `std::vector<A>`. With the default
    allocator this is bit-for-bit `std::vector<A>` — same type, same
    size, same cost.
  - `RoseLayer<T, Allocator = rose_default_allocator>` — binder now takes
    both axes (value type and allocator); `RoseLayer<T>::template F<A>`
    is unchanged for callers that only ever named `T`.
  - `RoseTreeFix<T>` — unchanged spelling and type (always the default
    allocator).
  - `rose<T>(value, children)` — unchanged spelling, unchanged type,
    unchanged cost.
  - New: `rose(std::allocator_arg_t, const Allocator&, T value,
    rose_children_t<Fix<RoseLayer<T,Allocator>::template F>, Allocator>
    children = {})` — the allocator-tagged smart constructor (see
    "Interface decided").
  - `RoseFFunctorImpl<T, A, Allocator>` / `RoseFFunctorMap` — generalized
    over `Allocator`; `fmap` builds the output vector on the **source**
    vector's own allocator, rebound to the mapped type.
  - `functor_typeclass<RoseF<T, A, Allocator>>` and
    `layer_fold_typeclass<RoseF<T, A, Allocator>>` — re-registered on the
    generalized shape with `Allocator` deduced, covering the default and
    any future pmr instantiation with one registration (no per-type
    duplication needed for WP-7).
  - New: `RoseFFmapAlloc<Allocator>` / `rose_fmap(alloc)` — the
    allocator-carrying fmap for unfold (see "Interface decided").
- `tests/beman/tree_algorithms/rose_tree.test.cpp`
  - All 8 prior test cases and 3 prior `static_assert`s unchanged and
    green (the file compiled clean against the generalized templates
    with no source changes needed in the untouched parts — the default
    path really is bit-identical).
  - Two new `static_assert`-covered constexpr functions (DEV-04): the
    allocator-tagged `rose()` and `rose_fmap(alloc)` each round-tripped
    at compile time on `rose_default_allocator`.
  - Two new runtime `TEST_CASE`s: `PmrRoseRoutesEveryLevelThroughTheResource`
    (manual bottom-up build via the allocator-tagged `rose()`) and
    `PmrUnfoldBuildsEntirelyFromThePool` (`unfold_fix` + `rose_fmap(a)`),
    both over a `monotonic_buffer_resource` with `null_memory_resource()`
    upstream — this is the WP-4 gate.

No other files changed; no new header, so no module-interface or CMake
edit was needed (`rose_tree.hpp` is already in the umbrella
`tree_algorithms.hpp`, matching WP-1/WP-2's DEV-A02 precedent).

## Interface decided

- **Pmr rose alias (for WP-7):**
  `Fix<RoseLayer<T, std::pmr::polymorphic_allocator<std::byte>>::template F>`
  — WP-7 should alias this as `pmr::RoseTreeFix<T>`. No new registration
  is needed to make it work: `functor_typeclass` and
  `layer_fold_typeclass` are already generic over `Allocator`, so this
  falls out exactly as the plan's "groundwork for WP-7" asked.
- **Allocator-tagged constructor:**
  `rose(std::allocator_arg_t, const Allocator& a, T value,
  rose_children_t<Fix<RoseLayer<T, Allocator>::template F>, Allocator>
  children = {})` — tag-first, per Decision 9. It re-homes whatever
  `children` vector it is handed onto `a` via `std::vector`'s
  allocator-extended move constructor, so the *node's own* children
  vector always ends up on the resource regardless of how the caller
  built the argument.
  **Important caller contract (see "Open questions" below): already-built
  subtrees must be `std::move`d into that vector, never brace-copied.**
- **Allocator-carrying fmap for unfold:**
  `rose_fmap(alloc)` → `RoseFFmapAlloc<Allocator>`, same shape/role as
  `expr_fmap(alloc)`. Pass it to the explicit-fmap `unfold_fix`/`refold`
  to route every built node's children vector through `alloc`'s
  resource:
  `unfold_fix<RoseLayer<T,PA>::template F>(coalgebra, rose_fmap(a), seed)`.
- **Fold/map path needs no allocator-carrying anything**: the generalized
  `functor_typeclass` fmap propagates the *source* vector's own
  allocator (rebound to the mapped type) automatically, so `fold_fix`,
  `refold`'s evaluated pass, and lookup-based `fold_map` all keep a pmr
  tree's intermediate `F<Result>` layers on the same resource for free —
  this is `rose`'s asymmetry with the Box-at-knot expression layer: there
  is no field-type problem here, so both directions (build and fold) are
  covered by ordinary generalization, and only *build-from-seed* (unfold)
  needed a dedicated allocator-carrying fmap, because a coalgebra's
  returned layer has no real resource to propagate in the first place.
- **Verbs unchanged**: no allocator overload on `unfold_fix`/`fold_fix`/
  `refold`. Confirmed, same as WP-5-expr.

## Verified

- `make compile` clean (no warnings) with the final header/test pair.
- `make test` = **85/85** (was 83; +2 new `TEST_CASE`s; the file also
  gained 2 new `static_assert`-covered constexpr functions, which do not
  add to the ctest count but are compiled and evaluated on every build).
- `make compile-headers` green (`rose_tree.hpp` builds standalone; no new
  header was added, `tree_algorithms.hpp` already covers it).
- Spot run: `ctest --test-dir .build/build-system -R RoseTree
  --output-on-failure` = 6/6.
- `PmrRoseRoutesEveryLevelThroughTheResource`: builds `(1 2 (3 4) 5)`
  bottom-up via the allocator-tagged `rose()` over a 16 KiB
  `monotonic_buffer_resource` with `null_memory_resource()` upstream;
  checks the root's children vector AND node 3's (grandchild-level)
  children vector both report `get_allocator().resource() == &pool`;
  folds it with non-commutative string concatenation (DEV-01) to
  `"12345"` through the generalized lookup registrations.
- `PmrUnfoldBuildsEntirelyFromThePool`: builds a fanout tree via
  `unfold_fix` + `rose_fmap(a)` over the same null-upstream setup; checks
  root and a first-level child's children vectors are on the pool; folds
  with a non-commutative digit-composing combine (DEV-01) to `2551`,
  matching the compile-time `rose_fmap` static_assert's expected value
  (same shape, same combine).
- All 8 pre-existing rose-tree test cases and 3 pre-existing
  `static_assert`s pass unmodified — the header's generalization is
  bit-for-bit backward compatible on the default-allocator path
  (confirmed by the fact that no line in the pre-existing test file
  needed to change).

## Deviations

None from the plan's WP-4 scope, files list, or gate.

One implementation-choice worth recording (not a deviation from the
plan, but a design decision the plan left open, per its own text: "decide
based on what the unfold path actually needs and record your
reasoning"):

- The generalized `functor_typeclass` fmap propagates the **source**
  vector's allocator forward (rebound to the mapped type), rather than
  defaulting or requiring a captured value. This is the opposite policy
  from `rose_fmap`/`expr_fmap`, which always use a **captured** allocator
  value. Both are correct for their own call site: fold/map layers
  already have a real, correctly-resourced source vector to propagate
  (built earlier by `rose(allocator_arg, …)` or a prior `fmap`), so
  propagating it is strictly better than forcing every fold call site to
  pass an allocator it doesn't otherwise need; unfold's coalgebra output
  has no such real vector (it's ordinarily default-built by user code
  blind to the target resource), so only a captured value works there.
  `rose_fmap(alloc)` exists precisely because the two policies cannot be
  unified into one fmap.

## Open questions / warnings for downstream

- **Brace-init-copies a pmr subtree, silently losing its resource — a
  foot-gun WP-7's ergonomics layer should design around or warn about.**
  `RoseTreeFix<T>` (default or pmr) is not itself allocator-aware
  (`std::uses_allocator` is not specialized for `Fix`, per D-A2/Decision
  9), so copying one is an ordinary deep copy: the nested children
  vector's own copy constructor consults
  `select_on_container_copy_construction`, and for
  `std::pmr::polymorphic_allocator` that **deliberately** returns a
  default-resource allocator (the standard's documented behavior, and
  exactly the container-rules Decision 9 commits to). Concretely: writing
  `rose_children_t<PmrTree, PA>{child1, child2}` (braced-init-list
  construction) **copies** `child1`/`child2` into the vector, and that
  copy silently strands the copy's own nested children off the intended
  pool. The fix (used throughout this WP's new tests) is to always
  `push_back(std::move(child))` already-built subtrees into a children
  vector, never brace-list them. This bit me once while writing the WP-4
  gate test (`PmrRoseRoutesEveryLevelThroughTheResource` originally
  failed exactly this way; see the test's comment for the full
  explanation) — I do not consider it a defect in the header (it is
  correct, standard container-copy behavior, and Decision 9 explicitly
  wants copies to reset via SOCCC), but it is a sharp edge for any
  hand-written pmr tree assembly. WP-7's convenience layer (e.g. a
  variadic children-list factory) should either build via moves
  internally or document the hazard prominently.
- **Lookup-based fold/fmap over a pmr rose tree works out of the box**
  (unlike the expression tree, where lookup-based *unfold* of a pmr tree
  cannot compile). There is no analogous restriction here: the
  generalized `functor_typeclass`/`layer_fold_typeclass` registrations
  cover fold, refold's evaluated pass, and fold_map identically for the
  default and any pmr instantiation. Only *unfold* needs the dedicated
  `rose_fmap(alloc)` with the explicit-fmap verb — WP-7/WP-8 should state
  this contrast plainly, since it's a genuine difference in how much
  "pmr coverage" each representation gets for free.
- Production Box-at-knot representations were already fully enumerated by
  WP-5-expr's handoff (`ExprF` converted; `BinaryTreeF` is the
  `shared_ptr`-spine path, different mechanism). This WP closes out the
  rose tree; the representation matrix remaining for WP-7 is: expression
  (Box-at-knot, done), rose (vector-children, done here), and whatever
  WP-5/WP-6 land for `BinaryTree`/fringe (`allocate_shared`).
