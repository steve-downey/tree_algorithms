# WP-6 handoff — Fringe tree

## Landed

- `include/beman/tree_algorithms/fringe_tree.hpp`
  - New design-comment paragraph (in the file's top-of-namespace essay)
    recording the allocator-awareness scope decision for this
    representation and why `leaf`/`empty`/`concat`/`cons`/`snoc`/
    `from_sequence` stay untagged.
  - `FringeTree<T>::branch(std::allocator_arg_t, const Allocator&,
    FringeTree, FringeTree)` — new static member template, added right
    after the existing `branch(FringeTree, FringeTree)`. Routes both
    spine `shared_ptr` allocations through `std::allocate_shared` with
    `a` rebound to `FringeTree`; computes the cached measure exactly as
    the untagged `branch` does.
  - `FringeTreeEmbedAllocFn<Allocator>` / `fringe_tree_embed_alloc(alloc)`
    — new callable type and builder function, added right after
    `fringe_tree_embed` in the same anchor block. The allocator-carrying
    embedding for `unfold_with`: `FringeEmpty`/`FringeLeaf` layers embed
    exactly as `fringe_tree_embed` does (no allocation to route); a
    `FringeBranch` layer embeds through the tagged `branch(allocator_arg,
    alloc, ...)`.
  - No new header; no other file in `include/` touched.
- `tests/beman/tree_algorithms/fringe_tree.test.cpp`
  - New includes: `<cstddef>`, `<memory>`, `<memory_resource>`.
  - New `using` for `fringe_tree_embed_alloc`.
  - New anonymous-namespace helper `CountingResource` (a
    `std::pmr::memory_resource` decorator counting `do_allocate`/
    `do_deallocate` calls it forwards to an upstream resource) — see
    "Open questions" for why this exists instead of the `get_allocator()`
    read-back the WP-4/WP-5 pmr tests use.
  - Two new `TEST_CASE`s (see "Verified" below); all 7 prior `TEST_CASE`s
    and both prior `static_assert`-covered constexpr functions are
    unchanged.
  - File was reformatted with the project's `clang-format`; no other
    content change from that pass.

No module-interface or `CMakeLists.txt` edit was made or needed:
`fringe_tree.hpp` was already listed in the umbrella
`tree_algorithms.hpp` (and thus reachable from `tree_algorithms.cppm`)
before this WP, and no new header was added — the same DEV-A02
non-departure WP-1/WP-2/WP-4/WP-5 recorded.

## Interface decided

- **Tagged spine factory** (the minimum the plan asked for):
  `FringeTree<T>::branch(std::allocator_arg_t, const Allocator& a,
  FringeTree<T> left, FringeTree<T> right) -> FringeTree<T>` — tag-first,
  per Decision 9. Rebinds `a` to `FringeTree<T>` once and reuses it for
  both `std::allocate_shared` calls.
- **Allocator-carrying embed for unfold_with** (WP-6's answer to the
  plan's open question, "decide whether the unfold path needs a
  dedicated allocator-carrying builder — it does, same reasoning as
  `rose_fmap`/`expr_fmap`):
  `fringe_tree_embed_alloc(alloc) -> FringeTreeEmbedAllocFn<Allocator>`.
  Pass it to `unfold_with` in place of `fringe_tree_embed`:
  `unfold_with<FringeTree<T>>(coalgebra, fmap_fn, fringe_tree_embed_alloc(a), seed)`.
- **Everything else stays untagged, by design, not oversight**:
  `leaf`, `empty`, `concat`, `cons`, `snoc`, `from_sequence` gained no
  allocator overload. Reasoning, recorded both here and in the header's
  design comment:
  - `leaf`/`empty` never allocate a spine node (a leaf's value sits
    directly in the `variant`; empty carries no state), so a tagged
    overload would take an allocator and silently discard it — strictly
    worse than not offering one.
  - `concat`/`cons`/`snoc`/`from_sequence` are compositions of
    `branch`/`leaf`/`empty` aimed at the default-allocator ergonomic
    path. "Build an entire fringe tree onto a supplied resource" already
    has two complete routes without them: nest tagged `branch()` calls
    directly (proved by the `PmrBranchRoutesEverySpineAllocationThroughThePool`
    test below), or drive the whole build through `unfold_with` +
    `fringe_tree_embed_alloc(alloc)` (proved by
    `PmrUnfoldBuildsEntirelyFromThePool`). Threading an allocator through
    four more compositional entry points for no gate/test that needs it
    would be scope creep against D-A2's spirit of minimal, justified
    additions.
- **Verbs unchanged**: no allocator overload on `fold_with`/`unfold_with`.
  Confirmed, matching WP-4/WP-5.
- **fold_with / the layer fold need no allocator-carrying counterpart**:
  folding a fringe tree never allocates a spine node (the projection
  hands out raw non-owning pointers into an existing tree); only
  building one does. This is the fringe-tree instance of the same
  fold-vs-unfold asymmetry WP-4 documents for the rose tree, just
  total on the fold side rather than partial.

## Verified

- `make compile` clean (final state, whole repo, after the concurrent
  WP-5 `binary_tree.hpp`/`binary_tree.test.cpp` work in this same
  working tree also reached a green state — see "Open questions").
- `make compile-headers` clean (`all_verify_interface_header_sets`);
  `fringe_tree.hpp` verified standalone alongside `binary_tree.hpp` and
  the umbrella `tree_algorithms.hpp`.
- `make test` = **90/90** at the point this WP finished (includes the
  concurrent WP-5 work landing in the same tree; WP-6's own contribution
  is +2 test cases over its own prior baseline).
- `ctest --test-dir .build/build-system -R FringeTree` = **9/9**:
  the 7 pre-existing `FringeTree - *` cases pass unmodified
  (`Constructors`, `ConcatEmptyIsIdentity`, `SequenceOrder`,
  `ViewsDecomposeOnTheQuotient`, `DirectFoldPinsShapeAndOrder`,
  `UnfoldWithMaintainsMeasureInvariant`,
  `FoldMapConcatFollowsTheSequence`), plus 2 new:
  - `FringeTree - PmrBranchRoutesEverySpineAllocationThroughThePool` —
    hand-assembles `((1 2) (3 4))` via three nested calls to the tagged
    `branch()`, over a `CountingResource` wrapping a
    `std::pmr::monotonic_buffer_resource` whose upstream is
    `std::pmr::null_memory_resource()`. Asserts `allocate_count == 6`
    (exactly two `allocate_shared` calls per branch node, three branch
    nodes) immediately after the build, checks `flatten()`/`measure()`,
    folds with the shape-and-order-pinning algebra (DEV-01) to
    `"((1 2) (3 4))"`, then — after the tree goes out of scope —
    asserts `deallocate_count == allocate_count` (balanced teardown, no
    leak).
  - `FringeTree - PmrUnfoldBuildsEntirelyFromThePool` — builds the same
    `[0, 8)`-splitting balanced tree as the pre-existing
    `UnfoldWithMaintainsMeasureInvariant` test, but through
    `unfold_with<Tree>(split_coalgebra, fmap_fn,
    fringe_tree_embed_alloc(a), Range{0, 8})` over the same
    `CountingResource`-wrapped null-upstream pool. Asserts
    `allocate_count == 14` (7 branch nodes in a full binary tree of 8
    leaves, 2 allocations each), checks `flatten()`/`measure()`, folds
    to the same `"(((0 1) (2 3)) ((4 5) (6 7)))"` shape string the
    default-allocator test produces, then asserts balanced teardown.
- `clang-format --dry-run -Werror` clean on both changed files.
- `grep` for banned spellings (`Fix{`, `Box{`, `Identity{`, `#pragma
  once`, relative includes, `using namespace`, `cata`/`ana`/`hylo`) in
  both changed files: no matches.
- All UUID transclusion anchors in `fringe_tree.hpp` verified paired and
  unmodified in content (`1484acca-…`, `f4f21f0a-…`, `0a841db5-…`,
  `a692305b-…`, `321edf34-…`, `e6cd1dd3-…`); the two new pieces of
  production code were inserted inside the two anchors whose sections
  they belong to (`1484acca-…` for the tagged `branch`,
  `321edf34-…` for the tagged embed), not outside them.

## Deviations

None from the plan's WP-6 scope, files list, or gate.

One implementation choice worth recording (not a deviation, but a
design decision the plan explicitly left to this WP's judgment, "decide
... and record your reasoning"):

- **DEV-A05 (locally numbered; managing agent may renumber against a
  concurrent WP-5 DEV-A05) — the pmr gate test proves routing via a
  counting `memory_resource`, not via `get_allocator()` read-back.**
  What the plan said: model the gate "on the WP-4 pmr tests" — those
  read `some_container.get_allocator().resource() == &pool` directly
  off the built structure. What was done instead: `std::shared_ptr`
  exposes no public accessor for the allocator `std::allocate_shared`
  used to build its control block (unlike `Box::get_allocator()` or
  `std::pmr::vector::get_allocator()`), so that read-back is not
  available for a `shared_ptr`-spine representation. `CountingResource`
  — a small `memory_resource` decorator counting
  `do_allocate`/`do_deallocate` calls it forwards — proves the same
  claim by a different, arguably stronger route: an allocation count
  landing exactly on the expected number of `allocate_shared` calls
  (not merely "some non-zero number") shows every spine allocation
  reached this resource and none other, and a matching deallocate count
  after teardown shows the balance holds. Why: this is a property of
  `std::shared_ptr`'s API surface, not a gap in this WP's header design;
  D-A5 itself says "the control block remembers the allocator", which is
  true but does not imply the allocator is queryable through the
  `shared_ptr` object. What it frees/costs downstream: WP-7's pmr surface
  and WP-8's paper section should know that `get_allocator()`-style
  assertions are not available for `BinaryTree`/`FringeTree` pmr tests;
  a `CountingResource`-style decorator (or an equivalent tracking
  resource) is the pattern to reuse for any future `shared_ptr`-spine
  pmr test, including WP-5's `BinaryTree` side if it has not already
  solved this the same way independently (it was being worked on
  concurrently with this WP in the same tree; `binary_tree.hpp`/
  `binary_tree.test.cpp` were not read closely by this WP beyond
  confirming they were out of scope, since editing them is WP-5's
  business, not WP-6's).

## Open questions / warnings for downstream

- **This WP ran concurrently with an in-progress WP-5 (`BinaryTree`)
  sub-agent in the same working tree, not an isolated worktree.**
  Partway through this WP's work, `make compile`/`make test` briefly
  failed on `binary_tree.test.cpp` (a `tracking_alloc`-based test
  hitting a `Box<A, Allocator>` default-construction issue and a
  functor-lookup mismatch over an allocator-parameterized
  `BinaryTreeF`) — confirmed, via `git status`/`git diff --stat`, to be
  entirely inside files this WP never touched
  (`include/beman/tree_algorithms/binary_tree.hpp`,
  `tests/beman/tree_algorithms/binary_tree.test.cpp`). By the time this
  WP finished, that sibling work had reached a green state on its own
  and the full-repo gate (`make compile`, `make compile-headers`, `make
  test` = 90/90) passed. Flagging this for the managing agent because
  the plan's dispatch note (§5) anticipates WP-3…6 running "in worktrees"
  for exactly this reason; this session ran directly in the shared
  checkout instead, so a transient red state during the overlap window
  was observed and is recorded here rather than silently smoothed over.
  It resolved before this WP's own gate was declared, and no line of
  `binary_tree.*` was written by this WP.
- **`shared_ptr` has no `get_allocator()`** — see DEV-A05 above; this is
  the one interface gap downstream pmr work over `BinaryTree` or
  `FringeTree` needs to plan around.
- **`fringe_tree_embed_alloc`'s `operator()` is intentionally not
  `constexpr`**, unlike `rose_fmap`/`expr_fmap` (which are `constexpr`,
  matching their Fix-native/Box-at-knot representations). `FringeTree`
  itself has never been constexpr-capable (the header's own top comment
  says so: "shared_ptr allocation is a runtime affair in C++23"), so
  there is no `static_assert` coverage expected or added for the new
  `branch`/`embed_alloc` additions — consistent with the pre-existing
  `branch`/`leaf`/`empty`/`concat`/`cons`/`snoc`/`from_sequence`, none
  of which carry `static_assert` coverage either. The layer machinery
  (`FringeTreeF`, its functor instance, the layer fold) remains fully
  constexpr and untouched by this WP.
- Representation matrix as it stands after this WP: expression
  (Box-at-knot, WP-5-expr, done), rose (vector-children, WP-4, done),
  fringe (`shared_ptr`-spine, WP-6, done here). `BinaryTree`
  (`shared_ptr`-spine, WP-5's other half) was in progress concurrently;
  its own handoff should confirm whether it independently arrived at a
  `CountingResource`-equivalent for its pmr gate test, per the DEV-A05
  note above.
