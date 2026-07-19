# WP-7 handoff — the `pmr` surface

## Landed

- NEW `include/beman/tree_algorithms/pmr.hpp` — `namespace beman::tree_algorithms::pmr`: one allocator alias, a pmr `Box<A>` alias, and thin allocator-bound factories over every representation (rose, expression, `BinaryTree`, fringe), each forwarding to the tag-first `std::allocator_arg` factory its owning WP already built.
  Runtime-only by design (documented in the header's top comment): `std::pmr::polymorphic_allocator` is not constexpr-usable, so nothing here is `constexpr`, and the default-allocator/constexpr paths of every other header are untouched.
  Three UUID-anchored blocks group the surface by representation, plus one anchor for the shared allocator alias/`Box`/`make_slot` primitives.
- NEW `tests/beman/tree_algorithms/pmr.test.cpp` — the flagship acceptance test: 8 type-level `static_assert`s (alias identity + `has_functor_instance`/`has_layer_fold_instance` over the pmr-allocator instantiation of every layer) plus 4 `TEST_CASE`s, one per representation, each building/folding (and, where the representation supports it, unfolding) a tree against a `std::pmr::monotonic_buffer_resource` whose upstream is `std::pmr::null_memory_resource()`.
- `include/beman/tree_algorithms/tree_algorithms.hpp` — added `#include <beman/tree_algorithms/pmr.hpp>` in alphabetical position, between `overloaded.hpp` and `recursion_schemes.hpp`.
- `include/beman/tree_algorithms/CMakeLists.txt` — added `pmr.hpp` to the `FILE_SET HEADERS FILES` list in both the `BEMAN_TREE_ALGORITHMS_USE_MODULES` and `else()` branches, alphabetical position after `overloaded.hpp`.
- `tests/beman/tree_algorithms/CMakeLists.txt` — added the `beman.tree_algorithms.tests.pmr` executable target (copied the `rose_tree` block shape exactly: `target_sources`, `target_link_libraries`, the `CXX_MODULE_STD` guard) plus its `catch_discover_tests(beman.tree_algorithms.tests.pmr)` line at the bottom.
- Did **not** touch `tree_algorithms.cppm` — confirmed by inspection (it only does `#include <beman/tree_algorithms/tree_algorithms.hpp>` inside its `export {}` block) and by `make compile` succeeding under the Asan config's C++20-modules dependency-scan path with `pmr.test.cpp` in the tree — the DEV-A02 precedent holds again: a new header already reachable through the umbrella `tree_algorithms.hpp` needs no separate module-interface edit.
- Did **not** touch `docs/notes/allocator/DEVIATIONS.md` (managing agent owns the ledger).

## Interface decided

All names live in `namespace beman::tree_algorithms::pmr`.

- **Allocator alias**: `using allocator_type = std::pmr::polymorphic_allocator<std::byte>;` — every factory below takes `const allocator_type&` and accepts a bare `std::pmr::memory_resource*` at the call site via `polymorphic_allocator`'s converting constructor.
- **`Box<A>`**: `template <typename A> using Box = beman::tree_algorithms::Box<A, std::allocator_traits<allocator_type>::rebind_alloc<A>>;` — i.e. `Box<A, std::pmr::polymorphic_allocator<A>>`, exactly what the tag-first `make_box<A>(std::allocator_arg, allocator_type{...}, ...)` already returns.
- **`make_slot<A>(a, args...)`** → `child_slot_t<A, allocator_type>`. Forwards to `beman::tree_algorithms::make_slot<A>(std::allocator_arg, a, args...)`.
- **Rose tree** (WP-4):
  - `RoseTreeFix<T> = Fix<RoseLayer<T, allocator_type>::template F>`.
  - `rose(a, value, children = {}) -> RoseTreeFix<T>`.
  - `rose_fmap(a) -> RoseFFmapAlloc<allocator_type>` — pass to `unfold_fix`/`refold` for allocator-aware unfold.
- **Expression tree** (WP-5-expr):
  - `Expr = Fix<ExprLayer<allocator_type>::template F>`.
  - `const_node(a, v) -> Expr`, `add_node(a, l, r) -> Expr`, `mul_node(a, l, r) -> Expr` — **new** smart constructors, not forwards: `expression.hpp` itself has no tagged `add_node`/`mul_node` (its allocator-aware build path is `expr_fmap` + `unfold_fix`, per WP-5-expr's own handoff, which explicitly deferred "wrap the coalgebra/smart-ctor ergonomics" to WP-7). Built from the same tagged `make_slot`/`wrap_fix` every other tagged factory uses — no new construction mechanism, no logic beyond what those primitives already do.
  - `expr_fmap(a) -> ExprFFmapAlloc<allocator_type>` — pass to `unfold_fix`/`refold`; lookup-based unfold of a pmr expression tree still does not compile (WP-5-expr's documented restriction), unaffected by this WP.
- **BinaryTree** (WP-5):
  - `BinaryTreeFix<T> = Fix<BinaryTreeLayer<T, allocator_type>::template F>`.
  - `leaf(a, value) -> BinaryTree<T>`, `node(a, value, left, right) -> BinaryTree<T>`, `make_ptr(a, tree) -> std::shared_ptr<BinaryTree<T>>`.
  - `to_fix(a, tree) -> BinaryTreeFix<T>`, `from_fix(a, fixed) -> BinaryTree<T>` (T deduced via `binary_tree_element`, same as the base overload).
  - No `pmr::BinaryTree<T>` alias: the shared_ptr spine carries no allocator in its own type (D-A5), so `beman::tree_algorithms::BinaryTree<T>` *is* the pmr type once built through these factories.
- **Fringe tree** (WP-6):
  - `branch(a, left, right) -> FringeTree<T>`.
  - `fringe_tree_embed_alloc(a) -> FringeTreeEmbedAllocFn<allocator_type>` — pass to `unfold_with`.
  - No `pmr::FringeTree<T>` alias, for the same D-A5 reason as `BinaryTree<T>`; no Fix form exists for this representation at all (WP-6).
- **Registration**: confirmed, not added — the pmr rose layer (and every other pmr-allocator layer instantiation) is already reachable through the generic `functor_typeclass`/`layer_fold_typeclass` registrations WP-4/WP-5/WP-5-expr wrote with `Allocator` deduced. Nothing in `pmr.hpp` specializes either lookup variable; the 8 type-level `static_assert`s in the test file (`has_functor_instance<...>`, `has_layer_fold_instance<...>` over each layer at `pmr::allocator_type`) are the proof this "falls out" claim holds for all four representations, not just rose.

## Verified

- `make compile` clean (Asan config; gcc toolchain), `pmr.test.cpp` builds and links with no warnings.
- `make test`: **94/94** (was 90; +4 new `TEST_CASE`s, 0 regressions — every pre-existing test passes with no source line changed).
- New tests, by name, all green under `ctest --test-dir .build/build-system -R Pmr`:
  - `Pmr - RoseTreeBuildFoldAndUnfoldAgainstNullUpstream` — hand-assembles a 5-node tree via `pmr::rose` (moving already-built subtrees per WP-4's brace-init-copy foot-gun), asserts `get_allocator().resource() == &pool` and `get_allocator() == a` at the root and a grandchild level, folds with non-commutative string concatenation to `"12345"` via the lookup tier, then unfolds a fanout tree from a fresh seed via `unfold_fix` + `pmr::rose_fmap(a)`, re-asserts resource identity, and folds to `2551` (matching WP-4's own compile-time expectation for the same coalgebra).
  - `Pmr - ExpressionTreeBuildFoldAndUnfoldAgainstNullUpstream` — builds `(1 + 2) * 3` via `pmr::const_node`/`add_node`/`mul_node`, asserts knot-Box resource/allocator identity two levels deep, folds via lookup with a non-commutative parenthesizing print algebra to `"((1 + 2) * 3)"`, then unfolds `[0, 4)` via `unfold_fix` + `pmr::expr_fmap(a)` and folds the result to `"((0 + 1) + (2 + 3))"`.
  - `Pmr - BinaryTreeToFixFoldFromFixAgainstNullUpstream` — builds the shared_ptr spine via `pmr::node`/`leaf` (4 `allocate_shared` calls, exact count via `CountingResource` per DEV-A05), converts with `pmr::to_fix` (4 more, knot Boxes — asserts `get_allocator().resource()`/`== a` two levels deep), folds via lookup with a non-commutative in-order show algebra to `"((4 2 5) 1 3)"`, rebuilds with `pmr::from_fix` (4 more `allocate_shared` calls, total 12), then confirms `deallocate_count == allocate_count` after the whole structure goes out of scope.
  - `Pmr - FringeTreeBranchAndUnfoldAgainstNullUpstream` — hand-assembles `((1 2) (3 4))` via nested `pmr::branch` (6 `allocate_shared` calls via `CountingResource`), folds via `fold_with` + the tree's own projection (no Fix form exists for this representation) to `"((1 2) (3 4))"`, then unfolds `[0, 4)` via `unfold_with` + `pmr::fringe_tree_embed_alloc(a)` (6 more, total 12), folds the result to `"((0 1) (2 3))"`, and confirms balanced teardown.
- `make compile-headers`: green — `pmr.hpp` verified standalone (re-checked after a forced `touch` to rule out a stale-cache pass, and again after the `clang-format` pass below).
- `clang-format --dry-run -Werror` on both new files: clean (after one in-place `clang-format -i` pass to fix a first-draft formatting drift — re-verified green, and `make test`/`make compile-headers` re-run clean afterward).
- `grep` for banned spellings (`Fix{`, `Box{`, `Identity{`, `#pragma once`, `using namespace`, `cata`/`ana`/`hylo`) and relative includes in both new files: no matches.
- `git diff --stat` / `git status`: touches exactly the WP-7 file list — `include/beman/tree_algorithms/{CMakeLists.txt,tree_algorithms.hpp}`, `tests/beman/tree_algorithms/CMakeLists.txt` modified; `include/beman/tree_algorithms/pmr.hpp`, `tests/beman/tree_algorithms/pmr.test.cpp` new. Nothing under `infra/`.

## Deviations

None from the plan's WP-7 scope, files list, or gate.

One implementation choice worth recording, in the same spirit as prior WPs' non-departure notes (a decision the plan left to this WP's judgment, not a contradiction of anything decided):

- **`pmr::const_node`/`add_node`/`mul_node` are new code, not forwards**, unlike every other factory in this header. `expression.hpp` (WP-5-expr) never added tagged smart constructors of its own — only `expr_fmap(alloc)` — and its handoff note says so explicitly: *"WP-7 will alias this as `pmr::Expr` and wrap the coalgebra/smart-ctor ergonomics."* So these three functions had to be written here, using the same tagged `make_slot`/`wrap_fix` primitives every other tagged factory in the codebase already uses. This is exactly the ergonomics layer D-A4 asks WP-7 to provide ("keep these thin — they bind the allocator and delegate; do not reimplement logic") applied to a gap the owning WP left open by design, not a deviation from either WP's scope.
- **`pmr::make_box` was deliberately not added.** D-A4's factory list (rose, `make_slot`, the expr/binary smart constructors, `to_fix`, `from_fix`, fringe `branch`, the fmaps/embeds) does not name `make_box`, and the generic `make_box<A>(std::allocator_arg, alloc, args...)` (box.hpp) already works directly with `pmr::allocator_type` supplied as `alloc` — a thin pmr-namespaced forward would add a name without adding ergonomics beyond what `pmr::make_slot` already covers for the child-slot case. Left out to stay inside the requested scope; trivial to add later if a caller needs the bare-`Box` spelling.

## Open questions / warnings for downstream

- **File-prolog nit (flagged mid-task, not fixed here):** the `cpp-house-style` skill's generic Beman template puts a full repo-relative path + Emacs mode line on line 1 of every source file (`// include/beman/.../pmr.hpp   -*-C++-*-`). Every existing header and test file in this repository — `box.hpp`, `rose_tree.hpp`, `rose_tree.test.cpp`, etc. — omits that line and opens directly with the SPDX line. `pmr.hpp`/`pmr.test.cpp` were written to match the repository's actual, established convention rather than the skill's generic template, per the skill's own "when unspecified, match the surrounding code" rule. This is a repo-wide, pre-existing discrepancy between the skill doc and the codebase, not something introduced by this WP — noted here as a cleanup nit for a future pass (e.g. as part of WP-8's docs cleanup, or a dedicated pass across every file), not something this WP should fix unilaterally across files it doesn't own.
- **`CountingResource` is duplicated, not shared.** `pmr.test.cpp` carries its own private copy of the `CountingResource` class (same shape as `fringe_tree.test.cpp`'s, per DEV-A05), because no shared test-utility header exists in this codebase for it. WP-8 (or a future test-infra pass) may want to promote it to a shared header under `tests/beman/tree_algorithms/` if a fifth shared_ptr-spine representation ever needs it; not done here since it is out of WP-7's file scope and each existing copy is small.
- **Representation matrix is now closed for pmr.** All four representations landed by WP-4/WP-5/WP-5-expr/WP-6 have a pmr factory surface and an acceptance test against a null-upstream pool: rose (Fix-native, vector children), expression (Box-at-knot), `BinaryTree` (shared_ptr spine + Box-at-knot Fix side), fringe (shared_ptr spine, no Fix form). WP-8's paper section can transclude directly from the three UUID-anchored blocks in `pmr.hpp` plus the shared allocator/`Box`/`make_slot` anchor.
