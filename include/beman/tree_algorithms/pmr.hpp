// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_TREE_ALGORITHMS_PMR_HPP
#define BEMAN_TREE_ALGORITHMS_PMR_HPP

#include <beman/tree_algorithms/config.hpp>

#if BEMAN_TREE_ALGORITHMS_USE_MODULES() && !defined(BEMAN_TREE_ALGORITHMS_INCLUDED_FROM_INTERFACE_UNIT)

import beman.tree_algorithms;

#else

    #include <beman/tree_algorithms/binary_tree.hpp>
    #include <beman/tree_algorithms/box.hpp>
    #include <beman/tree_algorithms/child_slot.hpp>
    #include <beman/tree_algorithms/expression.hpp>
    #include <beman/tree_algorithms/fix.hpp>
    #include <beman/tree_algorithms/fringe_tree.hpp>
    #include <beman/tree_algorithms/rose_tree.hpp>

    #if !BEMAN_TREE_ALGORITHMS_USE_MODULES()
        #include <memory>
        #include <memory_resource>
        #include <utility>
    #endif

namespace beman::tree_algorithms::pmr {

// The pmr surface: one place collecting an allocator alias plus thin,
// allocator-bound factories over every representation this library ships
// (Decision 9, D-A4). Every factory here forwards to the tag-first
// std::allocator_arg factory the owning representation's own header
// already provides (WP-2 through WP-6) — no logic is reimplemented, only
// the allocator is bound and the std::allocator_arg tag is hidden, so a
// caller writes pmr::rose(a, v) instead of
// rose(std::allocator_arg, a, v).
//
// RUNTIME-ONLY. std::pmr::polymorphic_allocator dispatches every
// allocation through a virtual std::pmr::memory_resource*, so it is not a
// constexpr-usable type; nothing in this header is exercised at compile
// time, and it adds nothing to the constexpr contract (Decision 7,
// DEV-04) that the default-allocator paths of every other header already
// satisfy. Those paths — Box<A, std::allocator<A>>, the untagged smart
// constructors, the default Fix aliases — are completely unaffected by
// this header's existence: it only adds new names, spelled under
// beman::tree_algorithms::pmr, that resolve to std::pmr::polymorphic_
// allocator<std::byte> at every one of the allocator-parameterized
// representations landed by the earlier work packages.
//
// Every representation gets a factory; not every representation gets an
// alias for a distinct pmr TYPE, because not every representation has
// one:
//  - the rose tree, expression tree, and BinaryTree's Fix side are all
//    Fix<Layer<T, Allocator>::template F> for some layer binder, so a
//    pmr instantiation IS a distinct type, aliased here
//    (RoseTreeFix<T>, Expr, BinaryTreeFix<T>).
//  - BinaryTree<T>'s shared_ptr side and FringeTree<T> carry no
//    allocator in their own type (D-A5: the shared_ptr control block
//    remembers the allocator that built it, not the object), so there
//    is no separate "pmr::BinaryTree<T>"/"pmr::FringeTree<T>" type to
//    alias — beman::tree_algorithms::BinaryTree<T> and ::FringeTree<T>
//    ARE the pmr type once built through the factories below, and this
//    namespace supplies only the allocator-bound factory ergonomics for
//    them.

// d91724ee-59a9-4e14-8681-2a140cef0266
/** The allocator every alias and factory in this namespace binds to: a
 * byte-typed std::pmr::polymorphic_allocator, rebound to the element or
 * node type at each construction site exactly as the tag-first factories
 * this namespace wraps already do. Accepts a std::pmr::memory_resource*
 * directly at any call site below (polymorphic_allocator's converting
 * constructor), so callers need not spell out the allocator type. */
using allocator_type = std::pmr::polymorphic_allocator<std::byte>;

/** A pmr Box<A>: the knot storage (box.hpp) rebound to allocator_type at
 * A, i.e. Box<A, std::pmr::polymorphic_allocator<A>> — exactly the type
 * the tag-first make_box<A>(std::allocator_arg, allocator_type{...},
 * args...) already returns. Provided as a name for spelling a pmr knot
 * Box explicitly (e.g. in a hand-written layer literal); no allocator
 * awareness is added beyond what box.hpp already generalized. */
template <typename A>
using Box = beman::tree_algorithms::Box<A, typename std::allocator_traits<allocator_type>::template rebind_alloc<A> >;

/** Allocator-bound child_slot factory: builds the child slot for @p A —
 * a knot Box or an inline optional, exactly as child_slot.hpp's
 * allocator-tagged make_slot decides — routed through @p a, tag hidden.
 */
template <typename A, typename... Args>
auto make_slot(const allocator_type& a, Args&&... args) -> child_slot_t<A, allocator_type> {
    return beman::tree_algorithms::make_slot<A>(std::allocator_arg, a, std::forward<Args>(args)...);
}
// d91724ee-59a9-4e14-8681-2a140cef0266 end

// ---------------------------------------------------------------------
// Rose tree: Fix-native, vector children (WP-4).
// ---------------------------------------------------------------------

// 3c0d2dc2-2fbd-4fe2-93ae-bc3c918c292c
/** The pmr rose tree over @p T: rose_tree.hpp's own alias, with the
 * allocator axis fixed to allocator_type. Falls out of RoseLayer's
 * generalized functor_typeclass/layer_fold_typeclass registrations
 * (WP-4) with no further registration needed here — the rose layer is
 * wired into the lookup verbs for this allocator exactly as it is for
 * std::allocator. */
template <typename T>
using RoseTreeFix = Fix<RoseLayer<T, allocator_type>::template F>;

/** Allocator-bound rose(): builds a node whose children vector is on
 * @p a's resource, tag hidden. */
template <typename T>
auto rose(const allocator_type& a, T value, rose_children_t<RoseTreeFix<T>, allocator_type> children = {})
    -> RoseTreeFix<T> {
    return beman::tree_algorithms::rose(std::allocator_arg, a, std::move(value), std::move(children));
}

/** Allocator-bound rose_fmap(): the allocator-carrying fmap for
 * unfold_fix/refold over a pmr rose layer, bound to @p a's resource. */
inline auto rose_fmap(const allocator_type& a) -> RoseFFmapAlloc<allocator_type> {
    return beman::tree_algorithms::rose_fmap(a);
}
// 3c0d2dc2-2fbd-4fe2-93ae-bc3c918c292c end

// ---------------------------------------------------------------------
// Expression tree: Box-at-knot (WP-5-expr, Option A).
// ---------------------------------------------------------------------

// c8690047-802d-4423-af0e-9921f54cd6ad
/** The pmr expression tree: expression.hpp's Expr alias, with the
 * allocator axis fixed to allocator_type. expression.hpp itself offers
 * no allocator-tagged const_node/add_node/mul_node (its allocator-aware
 * build path is expr_fmap + unfold_fix, per WP-5-expr's handoff), so the
 * three smart constructors below are this namespace's own — built from
 * the same tagged make_slot and wrap_fix every representation's tagged
 * factories use, not a new construction mechanism. */
using Expr = Fix<ExprLayer<allocator_type>::template F>;

/** Build a constant leaf holding @p v; a constant has no recursive
 * position, so @p a allocates nothing here — accepted for uniform
 * tag-hidden spelling with add_node/mul_node. */
inline auto const_node([[maybe_unused]] const allocator_type& a, int v) -> Expr {
    return beman::tree_algorithms::wrap_fix<ExprLayer<allocator_type>::template F>(
        typename ExprLayer<allocator_type>::template F<Expr>{Const<Expr>{v}});
}

/** Build @p l + @p r, whose knot Boxes route through @p a. */
inline auto add_node(const allocator_type& a, Expr l, Expr r) -> Expr {
    return beman::tree_algorithms::wrap_fix<ExprLayer<allocator_type>::template F>(
        typename ExprLayer<allocator_type>::template F<Expr>{
            Add<Expr, allocator_type>{make_slot<Expr>(a, std::move(l)), make_slot<Expr>(a, std::move(r))}});
}

/** Build @p l * @p r, whose knot Boxes route through @p a. */
inline auto mul_node(const allocator_type& a, Expr l, Expr r) -> Expr {
    return beman::tree_algorithms::wrap_fix<ExprLayer<allocator_type>::template F>(
        typename ExprLayer<allocator_type>::template F<Expr>{
            Mul<Expr, allocator_type>{make_slot<Expr>(a, std::move(l)), make_slot<Expr>(a, std::move(r))}});
}

/** Allocator-bound expr_fmap(): the allocator-carrying fmap for
 * unfold_fix/refold over a pmr expression layer, bound to @p a's
 * resource — expression.hpp's own allocator-aware build path (WP-5-expr:
 * lookup-based unfold of a pmr tree does not compile, by design). */
inline auto expr_fmap(const allocator_type& a) -> ExprFFmapAlloc<allocator_type> {
    return beman::tree_algorithms::expr_fmap(a);
}
// c8690047-802d-4423-af0e-9921f54cd6ad end

// ---------------------------------------------------------------------
// BinaryTree: shared_ptr spine + Box-at-knot Fix side (WP-5).
// ---------------------------------------------------------------------

// bcf74499-d261-47c6-92b4-ed628a5686eb
/** The pmr BinaryTree Fix side: binary_tree.hpp's own alias, with the
 * allocator axis fixed to allocator_type. BinaryTree<T> itself (the
 * shared_ptr spine) carries no allocator in its type — D-A5's control
 * block remembers it — so there is no separate pmr::BinaryTree<T> type;
 * the factories below build an ordinary BinaryTree<T> through @p a. */
template <typename T>
using BinaryTreeFix = Fix<BinaryTreeLayer<T, allocator_type>::template F>;

/** Allocator-bound leaf(): a leaf allocates nothing (no children to wrap
 * in a shared_ptr), so @p a is accepted and unused — uniform tag-hidden
 * spelling with node(). */
template <typename T>
auto leaf(const allocator_type& a, T value) -> BinaryTree<T> {
    return BinaryTree<T>::leaf(std::allocator_arg, a, std::move(value));
}

/** Allocator-bound node(): both children's shared_ptr control blocks
 * come from @p a's resource. */
template <typename T>
auto node(const allocator_type& a, T value, BinaryTree<T> left, BinaryTree<T> right) -> BinaryTree<T> {
    return BinaryTree<T>::node(std::allocator_arg, a, std::move(value), std::move(left), std::move(right));
}

/** Allocator-bound make_ptr(): allocate_shared through @p a's resource.
 */
template <typename T>
auto make_ptr(const allocator_type& a, BinaryTree<T> tree) -> std::shared_ptr<BinaryTree<T> > {
    return BinaryTree<T>::make_ptr(std::allocator_arg, a, std::move(tree));
}

/** Allocator-bound to_fix(): converts a BinaryTree<T> to its Fix form,
 * every knot Box on @p a's resource. */
template <typename T>
auto to_fix(const allocator_type& a, const BinaryTree<T>& tree) -> BinaryTreeFix<T> {
    return beman::tree_algorithms::to_fix(std::allocator_arg, a, tree);
}

/** Allocator-bound from_fix(): rebuilds a BinaryTree<T> from its Fix
 * form, every reconstructed shared_ptr child through @p a's resource. */
template <template <typename> class F>
auto from_fix(const allocator_type& a, const Fix<F>& fixed) {
    return beman::tree_algorithms::from_fix(std::allocator_arg, a, fixed);
}
// bcf74499-d261-47c6-92b4-ed628a5686eb end

// ---------------------------------------------------------------------
// Fringe tree: shared_ptr spine, no Fix form (WP-6).
// ---------------------------------------------------------------------

// 8ff4dbe6-cad8-4d3d-b6e8-c8e46167deaf
/** Allocator-bound branch(): both spine shared_ptr allocations route
 * through @p a's resource; the cached measure is computed exactly as
 * the untagged branch() computes it. FringeTree<T> carries no allocator
 * in its own type (same D-A5 reasoning as BinaryTree<T>'s spine), so
 * there is no separate pmr::FringeTree<T> alias — only this factory
 * ergonomics layer. */
template <typename T>
auto branch(const allocator_type& a, FringeTree<T> left, FringeTree<T> right) -> FringeTree<T> {
    return FringeTree<T>::branch(std::allocator_arg, a, std::move(left), std::move(right));
}

/** Allocator-bound fringe_tree_embed_alloc(): the allocator-carrying
 * embedding for unfold_with, routing every branch node's spine
 * allocation through @p a's resource. */
inline auto fringe_tree_embed_alloc(const allocator_type& a) -> FringeTreeEmbedAllocFn<allocator_type> {
    return beman::tree_algorithms::fringe_tree_embed_alloc(a);
}
// 8ff4dbe6-cad8-4d3d-b6e8-c8e46167deaf end

} // namespace beman::tree_algorithms::pmr

#endif // BEMAN_TREE_ALGORITHMS_USE_MODULES() &&
       // !defined(BEMAN_TREE_ALGORITHMS_INCLUDED_FROM_INTERFACE_UNIT)

#endif // BEMAN_TREE_ALGORITHMS_PMR_HPP
