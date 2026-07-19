// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_TREE_ALGORITHMS_ROSE_TREE_HPP
#define BEMAN_TREE_ALGORITHMS_ROSE_TREE_HPP

#include <beman/tree_algorithms/config.hpp>

#if BEMAN_TREE_ALGORITHMS_USE_MODULES() && !defined(BEMAN_TREE_ALGORITHMS_INCLUDED_FROM_INTERFACE_UNIT)

import beman.tree_algorithms;

#else

    #include <beman/tree_algorithms/fix.hpp>
    #include <beman/tree_algorithms/fold_map_lookup.hpp>
    #include <beman/tree_algorithms/functor.hpp>

    #if !BEMAN_TREE_ALGORITHMS_USE_MODULES()
        #include <cstddef>
        #include <functional>
        #include <memory>
        #include <type_traits>
        #include <utility>
        #include <vector>
    #endif

namespace beman::tree_algorithms {

// A third shape of tree: a value and arbitrarily many children.
//
// RoseF<T, A> is the base functor of the rose tree (multiway tree) — one
// value plus a vector of child positions. Where BinaryTree and the
// fringe tree were pre-existing representations adapted to the verbs,
// the rose tree is Fix-native: Fix<RoseF> IS the representation, there
// is no separate class to adapt, and no Box either — std::vector both
// supports incomplete element types and supplies the indirection Box
// exists to provide, so the recursive knot ties without extra plumbing.
// (Box would return if the children were a fixed-arity aggregate; the
// choice is per-layer, not global.)
//
// The rose tree earns its place in the representation matrix by what it
// removes: with a variable number of children there is no "between the
// children" position, so IN-ORDER TRAVERSAL DOES NOT EXIST for this
// shape. In-order is a binary tree's contract, not a property of trees.
// This is why the elementwise machinery leaves traversal order to each
// layer's registered fold (layer_fold_typeclass) instead of imposing a
// generic order: the generic thing is the fold, the order is the
// instance's. The rose layer fold below documents and implements this
// representation's contract — pre-order, value before children, children
// left to right.
//
// Allocator awareness (Decision 9, WP-4): because the children ARE a
// vector rather than a Box-at-knot field, this representation never
// hits the field-type problem that forced the Box-at-knot layers
// (ExprF and friends) to parameterize on the allocator and route
// pmr construction through an allocator-carrying fmap only. Here the
// allocator lives directly in the vector's type, at every position —
// knot and complete types alike — so both the constructor (rose()) and
// the functor instance carry it uniformly.

// ---------------------------------------------------------------------
// The base functor, its Layer binder, and the Fix form.
// ---------------------------------------------------------------------

// 8f0e2d0a-5c1f-4be4-9f1e-3b7a8c92d461
/** The allocator a rose layer's children vector defaults to. A fixed
 * (value-type-independent) spelling, mirroring expr_default_allocator
 * in expression.hpp: the children vector rebinds it to the element type
 * at every instantiation, so RoseF<T, A> names exactly the type the
 * default RoseTreeFix<T> holds. */
using rose_default_allocator = std::allocator<std::byte>;

/** The vector type holding one rose layer's children: @p A elements,
 * allocated through @p Allocator rebound to A. With the default
 * allocator this is exactly std::vector<A> — unchanged type, unchanged
 * cost. */
template <typename A, typename Allocator>
using rose_children_t = std::vector<A, typename std::allocator_traits<Allocator>::template rebind_alloc<A> >;

/** One layer of a rose (multiway) tree: a value plus zero or more child
 * positions in left-to-right order. No Box: vector supports incomplete
 * element types and already owns through indirection.
 * @tparam T element type stored at every node
 * @tparam A recursive position placeholder (not yet fixed)
 * @tparam Allocator allocator for the children vector, rebound to A;
 * std::allocator<std::byte> by default, giving std::vector<A> exactly
 * as before
 */
template <typename T, typename A, typename Allocator = rose_default_allocator>
struct RoseF {
    T                             value;
    rose_children_t<A, Allocator> children;
};

/** Binds the element type and allocator of RoseF, leaving the
 * unary-in-A alias template @c F that Fix and the verbs require. */
template <typename T, typename Allocator = rose_default_allocator>
struct RoseLayer {
    template <typename A>
    using F = RoseF<T, A, Allocator>;
};

/** The Fix form of a rose tree over @p T — here the representation
 * itself, not a conversion target. Always the default (stateless)
 * allocator; a pmr rose tree is Fix<RoseLayer<T, pmr-allocator>::template F>. */
template <typename T>
using RoseTreeFix = Fix<RoseLayer<T>::template F>;

/** Assemble a rose tree node from a value and its subtrees. */
template <typename T>
constexpr auto rose(T value, std::vector<RoseTreeFix<T> > children = {}) -> RoseTreeFix<T> {
    return wrap_fix<RoseLayer<T>::template F>(RoseF<T, RoseTreeFix<T> >{std::move(value), std::move(children)});
}

/** Allocator-extended rose: assembles a node whose children vector is
 * built through @p a. @p children, if supplied, is re-homed onto @p a
 * via std::vector's allocator-extended move constructor, so the
 * returned node's children vector always ends up on the supplied
 * resource regardless of how the caller happened to build the argument
 * — the mechanism by which recursively calling this overload routes
 * every level of a pmr rose tree through the resource.
 * allocator_arg_t-tag-first, per Decision 9's factory spelling. */
template <typename T, typename Allocator>
constexpr auto rose(std::allocator_arg_t,
                    const Allocator&                                                     a,
                    T                                                                    value,
                    rose_children_t<Fix<RoseLayer<T, Allocator>::template F>, Allocator> children = {})
    -> Fix<RoseLayer<T, Allocator>::template F> {
    using Tree       = Fix<RoseLayer<T, Allocator>::template F>;
    using ChildAlloc = typename std::allocator_traits<Allocator>::template rebind_alloc<Tree>;
    rose_children_t<Tree, Allocator> homed(std::move(children), ChildAlloc(a));
    return wrap_fix<RoseLayer<T, Allocator>::template F>(
        RoseF<T, Tree, Allocator>{std::move(value), std::move(homed)});
}
// 8f0e2d0a-5c1f-4be4-9f1e-3b7a8c92d461 end

// ---------------------------------------------------------------------
// Functor instance.
// ---------------------------------------------------------------------

// 0d3c6e8b-92f4-45f7-8a26-6c1de5b0a973
/** Functor primitive for a rose layer over @p Allocator: applies @p fn
 * to every child position, left to right; the node value copies across
 * untouched. The output children vector is built on the SOURCE
 * vector's own allocator, rebound to the mapped type — unlike the
 * Box-at-knot layers, nothing here needs a fixed field type, so a fold
 * over a pmr rose tree keeps its intermediate F<Result> layers on the
 * same resource for free. */
template <typename T, typename A, typename Allocator>
struct RoseFFunctorImpl {
    template <typename Fn>
    constexpr auto fmap(this auto&&, Fn&& fn, const RoseF<T, A, Allocator>& layer) {
        using B      = std::remove_cvref_t<std::invoke_result_t<Fn, const A&> >;
        using BAlloc = typename std::allocator_traits<Allocator>::template rebind_alloc<B>;
        rose_children_t<B, Allocator> children(BAlloc(layer.children.get_allocator()));
        children.reserve(layer.children.size());
        for (const A& child : layer.children) {
            children.push_back(std::invoke(fn, child));
        }
        return RoseF<T, B, Allocator>{layer.value, std::move(children)};
    }
};

/** Functor map for a rose layer: the fmap primitive plus the derived
 * operations from the Functor CRTP base. */
template <typename T, typename A, typename Allocator>
struct RoseFFunctorMap : Functor<RoseFFunctorImpl<T, A, Allocator> > {
    using RoseFFunctorImpl<T, A, Allocator>::fmap;
};

/** Registers RoseFFunctorMap as the Functor instance for every rose
 * layer, over any allocator (keyed on the RoseF shape, Allocator
 * deduced — covers the default and any pmr instantiation alike). */
template <typename T, typename A, typename Allocator>
inline constexpr auto functor_typeclass<RoseF<T, A, Allocator> > = RoseFFunctorMap<T, A, Allocator>{};

/** Allocator-carrying fmap for rose layers: identical in shape to the
 * functor primitive above, but ignores the source vector's own
 * allocator and always builds the destination vector on the captured
 * @p alloc VALUE. Needed for unfold: a coalgebra's returned layer has
 * no real allocator to propagate — its children vector is ordinarily
 * default-built by user code that knows nothing of the target
 * resource, so "propagate the source's allocator" would just propagate
 * a default-resource instance. Passing rose_fmap(alloc) to the
 * explicit-fmap unfold_fix/refold puts every node of the built tree on
 * @p alloc's resource — the same mechanism expression.hpp uses for
 * expr_fmap(alloc) (Decision 9, 2026-07-18 amendment). */
template <typename Allocator>
struct RoseFFmapAlloc {
    Allocator alloc;

    template <typename Fn, typename T, typename A>
    constexpr auto operator()(Fn&& fn, const RoseF<T, A, Allocator>& layer) const {
        using B      = std::remove_cvref_t<std::invoke_result_t<Fn, const A&> >;
        using BAlloc = typename std::allocator_traits<Allocator>::template rebind_alloc<B>;
        BAlloc                        bound_alloc(alloc);
        rose_children_t<B, Allocator> children(bound_alloc);
        children.reserve(layer.children.size());
        for (const A& child : layer.children) {
            children.push_back(std::invoke(fn, child));
        }
        return RoseF<T, B, Allocator>{layer.value, std::move(children)};
    }
};

/** Build an allocator-carrying fmap for rose layers over @p alloc. */
template <typename Allocator>
constexpr auto rose_fmap(const Allocator& alloc) -> RoseFFmapAlloc<Allocator> {
    return RoseFFmapAlloc<Allocator>{alloc};
}
// 0d3c6e8b-92f4-45f7-8a26-6c1de5b0a973 end

// ---------------------------------------------------------------------
// Elementwise layer fold (for fold_map).
// ---------------------------------------------------------------------

// 4b9d17f2-30a5-4e1c-b8d4-7f52a6c3e08d
/** Folds one RoseF layer elementwise. This representation's contractual
 * order is PRE-ORDER: the mapped node value first, then each child's
 * already-folded result, left to right. There is no in-order alternative
 * to contract for — with n children there is no single "between the
 * children" slot — which is the concrete demonstration that traversal
 * order belongs to the instance, not to the generic fold. The identity
 * parameter never contributes here: a rose layer has no absent
 * positions (a leaf is simply a node with zero children).
 */
struct RoseLayerFoldMap {
    template <typename MapFn, typename Combine, typename Result, typename T, typename Allocator>
    constexpr auto operator()(const MapFn&                       map_fn,
                              const Combine&                     combine,
                              [[maybe_unused]] const Result&     identity,
                              const RoseF<T, Result, Allocator>& layer) const -> Result {
        Result acc = map_fn(layer.value);
        for (const Result& child : layer.children) {
            acc = combine(acc, child);
        }
        return acc;
    }
};

inline constexpr RoseLayerFoldMap rose_layer_fold_map{};

/** Lookup registration: the layer fold keyed on the layer type, over
 * any allocator. There is no project_typeclass registration — the rose
 * tree is Fix-native, and the Fix overloads of the verbs already know
 * how to unwrap it. */
template <typename T, typename A, typename Allocator>
inline constexpr auto layer_fold_typeclass<RoseF<T, A, Allocator> > = rose_layer_fold_map;
// 4b9d17f2-30a5-4e1c-b8d4-7f52a6c3e08d end

} // namespace beman::tree_algorithms

#endif // BEMAN_TREE_ALGORITHMS_USE_MODULES() &&
       // !defined(BEMAN_TREE_ALGORITHMS_INCLUDED_FROM_INTERFACE_UNIT)

#endif // BEMAN_TREE_ALGORITHMS_ROSE_TREE_HPP
