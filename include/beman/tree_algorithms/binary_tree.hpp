// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_TREE_ALGORITHMS_BINARY_TREE_HPP
#define BEMAN_TREE_ALGORITHMS_BINARY_TREE_HPP

#include <beman/tree_algorithms/config.hpp>

#if BEMAN_TREE_ALGORITHMS_USE_MODULES() && !defined(BEMAN_TREE_ALGORITHMS_INCLUDED_FROM_INTERFACE_UNIT)

import beman.tree_algorithms;

#else

    #include <beman/tree_algorithms/box.hpp>
    #include <beman/tree_algorithms/child_slot.hpp>
    #include <beman/tree_algorithms/fix.hpp>
    #include <beman/tree_algorithms/fold_map_lookup.hpp>
    #include <beman/tree_algorithms/functor.hpp>
    #include <beman/tree_algorithms/recursion_schemes_lookup.hpp>

    // <cassert> is macro-only and therefore not importable; it is included
    // unconditionally so assert works in the module build as well.
    #include <cassert>

    #if !BEMAN_TREE_ALGORITHMS_USE_MODULES()
        #include <cstddef>
        #include <functional>
        #include <memory>
        #include <type_traits>
        #include <utility>
    #endif

namespace beman::tree_algorithms {

// Adapting an existing tree you don't own.
//
// BinaryTree<T> below is a faithful port of a pre-existing persistent
// binary tree (smd::tree::BinaryTree): value at every node, children held
// by shared_ptr so copies are cheap and subtrees are structurally shared.
// That representation is part of its identity — and it is not Fix-based,
// so the recursion verbs cannot run over it directly.
//
// Design choice: rather than rewrite the tree, we adapt it. BinaryTreeF
// is the base functor describing one layer of a value-at-every-node
// binary tree (nullable child slots mirror the tree's nullable
// shared_ptr children; see child_slot.hpp — Box at the knot, inline
// storage at complete types), with a Functor instance registered for it, and
// to_fix/from_fix convert between the shared_ptr representation and
// Fix form. The same fold_fix/unfold_fix/refold that run over ExprF run
// unchanged over the converted tree; only the adapter knows how the tree
// is really stored. The conversions copy — an honest cost, consistent
// with the value-semantics stance of Decision 6.
//
// BinaryTree itself is not constexpr-capable (shared_ptr allocation is a
// runtime affair in C++23); the BinaryTreeF/Fix side is fully constexpr.

// ---------------------------------------------------------------------
// The existing tree, ported as-is.
// ---------------------------------------------------------------------

// f6f4cee4-9c55-4add-b38b-496936319294
/** Persistent binary tree where every node carries a value.
 * Nodes are either leaves (no children) or internal nodes with left and
 * right subtrees. Sharing is structural: subtrees are held by shared_ptr,
 * so copies are cheap and the tree is immutable once built.
 * @tparam T element type stored at every node
 */
template <class T>
class BinaryTree {
    T                           d_value;
    std::shared_ptr<BinaryTree> d_left;
    std::shared_ptr<BinaryTree> d_right;

  public:
    using value_type = T;

    /** Construct a leaf node holding @p value (no children). */
    static auto leaf(T value) -> BinaryTree { return BinaryTree(std::move(value), {}, {}); }

    /** Construct an internal node with @p value and two children. */
    static auto node(T value, BinaryTree left, BinaryTree right) -> BinaryTree {
        return BinaryTree(std::move(value),
                          std::make_shared<BinaryTree>(std::move(left)),
                          std::make_shared<BinaryTree>(std::move(right)));
    }

    /** Alias for node(); prefer node() in new code. */
    static auto branch(T value, BinaryTree left, BinaryTree right) -> BinaryTree {
        return node(std::move(value), std::move(left), std::move(right));
    }

    /** Low-level constructor accepting pre-built child shared_ptrs.
     * Null pointers represent absent children. No allocator-tagged
     * overload: the children are already-built shared_ptrs, so whatever
     * allocator produced them (e.g. the tagged make_ptr below) already
     * did the only allocation this constructor could route; there is
     * nothing left for a tag to change here.
     */
    static auto from_children_ptrs(T value, std::shared_ptr<BinaryTree> left, std::shared_ptr<BinaryTree> right)
        -> BinaryTree {
        return BinaryTree(std::move(value), std::move(left), std::move(right));
    }

    /** Heap-allocate a copy of @p tree and return the owning pointer. */
    static auto make_ptr(BinaryTree tree) -> std::shared_ptr<BinaryTree> {
        return std::make_shared<BinaryTree>(std::move(tree));
    }

    /** Allocator-extended leaf factory: uniform tag-first spelling with
     * node() (Decision 9), so generic code can build a leaf or an
     * internal node the same way. A leaf allocates nothing — it has no
     * children to wrap in a shared_ptr — so @p alloc is accepted and
     * unused; there is no allocator to store (D-A5: the shared_ptr
     * control block is the only place an allocator lives in this
     * representation, and a leaf has no control block of its own here
     * either). */
    template <typename Allocator>
    static auto leaf(std::allocator_arg_t, const Allocator&, T value) -> BinaryTree {
        return BinaryTree(std::move(value), nullptr, nullptr);
    }

    /** Allocator-extended internal-node factory: builds via
     * allocate_shared (through the tagged make_ptr below), so both
     * children's control blocks — and the BinaryTree objects they own —
     * come from @p alloc's resource. Tag-first, per Decision 9. */
    template <typename Allocator>
    static auto node(std::allocator_arg_t, const Allocator& alloc, T value, BinaryTree left, BinaryTree right)
        -> BinaryTree {
        return BinaryTree(std::move(value),
                          make_ptr(std::allocator_arg, alloc, std::move(left)),
                          make_ptr(std::allocator_arg, alloc, std::move(right)));
    }

    /** Allocator-extended heap-allocate: builds the owning shared_ptr via
     * allocate_shared, so the combined control-block-and-object
     * allocation comes from @p alloc's resource. The shared_ptr control
     * block remembers the allocator (D-A5), so BinaryTree itself stores
     * none — no stored allocator member is added to this class. */
    template <typename Allocator>
    static auto make_ptr(std::allocator_arg_t, const Allocator& alloc, BinaryTree tree)
        -> std::shared_ptr<BinaryTree> {
        return std::allocate_shared<BinaryTree>(alloc, std::move(tree));
    }

    /** Return the value stored at this node. */
    auto value() const -> const T& { return d_value; }

    /** True when this node has a left child. */
    auto has_left() const -> bool { return static_cast<bool>(d_left); }
    /** True when this node has a right child. */
    auto has_right() const -> bool { return static_cast<bool>(d_right); }

    /** Return the left child; precondition: has_left(). */
    auto left() const -> const BinaryTree& {
        assert(d_left);
        return *d_left;
    }

    /** Return the right child; precondition: has_right(). */
    auto right() const -> const BinaryTree& {
        assert(d_right);
        return *d_right;
    }

    /** Shared pointer to the left child; may be null. */
    auto left_ptr() const -> const std::shared_ptr<BinaryTree>& { return d_left; }
    /** Shared pointer to the right child; may be null. */
    auto right_ptr() const -> const std::shared_ptr<BinaryTree>& { return d_right; }

  private:
    BinaryTree(T value, std::shared_ptr<BinaryTree> left, std::shared_ptr<BinaryTree> right)
        : d_value(std::move(value)), d_left(std::move(left)), d_right(std::move(right)) {}
};
// f6f4cee4-9c55-4add-b38b-496936319294 end

// ---------------------------------------------------------------------
// The base functor describing one layer of that tree.
// ---------------------------------------------------------------------

// 27aeff53-a371-43b6-9037-135d5d008c26
/** The allocator a BinaryTreeF layer's Box-at-knot children default to. A
 * fixed (value-type-independent) spelling, mirroring
 * expr_default_allocator / rose_default_allocator: child_slot rebinds it
 * to Fix at the knot, so this is Box<Fix, std::allocator<Fix>> exactly as
 * before (Decision 9, 2026-07-18 WP-2 amendment). */
using binary_tree_default_allocator = std::allocator<std::byte>;

/** One layer of a value-at-every-node binary tree: a value plus nullable
 * children in child slots. A disengaged slot mirrors an absent child,
 * exactly as a null shared_ptr does in BinaryTree. The slot is a Box
 * exactly when A is the fixed point (the knot, where A is incomplete)
 * and inline std::optional storage at every complete type — so the
 * F<Result> layers a fold materializes allocate nothing. Parameterized
 * on the allocator so a stateful (pmr) allocator can live in the knot
 * Box (Decision 9); the default is the stateless std::allocator,
 * unchanged.
 * @tparam T element type stored at every node
 * @tparam A recursive position placeholder (not yet fixed)
 * @tparam Allocator allocator carried at the knot
 */
template <typename T, typename A, typename Allocator = binary_tree_default_allocator>
struct BinaryTreeF {
    T                          value;
    child_slot_t<A, Allocator> left;
    child_slot_t<A, Allocator> right;
};

/** Binds the allocator of a BinaryTreeF layer, leaving the unary-in-A
 * alias template @c F that Fix and the verbs require (à la RoseLayer<T>,
 * ExprLayer<Allocator>). */
template <typename T, typename Allocator = binary_tree_default_allocator>
struct BinaryTreeLayer {
    template <typename A>
    using F = BinaryTreeF<T, A, Allocator>;
};

/** The Fix form of a value-at-every-node binary tree over @p T, over the
 * default (stateless) allocator. A pmr binary tree is
 * Fix<BinaryTreeLayer<T, pmr-allocator>::template F>. */
template <typename T>
using BinaryTreeFix = Fix<BinaryTreeLayer<T>::template F>;
// 27aeff53-a371-43b6-9037-135d5d008c26 end

// ---------------------------------------------------------------------
// Functor instance.
// ---------------------------------------------------------------------

// 91fef612-39b5-4424-8ab5-7d8c80997e2c
/** Functor primitive for a BinaryTreeF layer over @p Allocator: applies
 * @p fn to each engaged child, left before right; absent (disengaged)
 * children stay absent. The node value copies across untouched. This
 * primitive boxes with the default allocator, which is correct for the
 * default (stateless) tree and for every fold (fold layers store
 * children inline at a complete Result type, so the allocator type
 * never reaches a Box). This WP's allocator-aware build path is to_fix,
 * not unfold_fix, so — unlike ExprF/RoseF — no allocator-carrying fmap
 * is needed here; the restriction is recorded for symmetry with those
 * representations' handoffs.
 */
template <typename T, typename A, typename Allocator>
struct BinaryTreeFFunctorImpl {
    template <typename Fn>
    constexpr auto fmap(this auto&&, Fn&& fn, const BinaryTreeF<T, A, Allocator>& layer) {
        using B = std::remove_cvref_t<std::invoke_result_t<Fn, const A&> >;
        return BinaryTreeF<T, B, Allocator>{
            layer.value,
            layer.left ? make_slot<B>(std::invoke(fn, *layer.left)) : child_slot_t<B, Allocator>{},
            layer.right ? make_slot<B>(std::invoke(fn, *layer.right)) : child_slot_t<B, Allocator>{},
        };
    }
};

/** Functor map for a BinaryTreeF layer: the fmap primitive plus the
 * derived operations from the Functor CRTP base. */
template <typename T, typename A, typename Allocator>
struct BinaryTreeFFunctorMap : Functor<BinaryTreeFFunctorImpl<T, A, Allocator> > {
    using BinaryTreeFFunctorImpl<T, A, Allocator>::fmap;
};

/** Registers BinaryTreeFFunctorMap as the Functor instance for every
 * BinaryTreeF layer, over any allocator (Allocator deduced — covers the
 * default and any pmr instantiation with one registration). */
template <typename T, typename A, typename Allocator>
inline constexpr auto functor_typeclass<BinaryTreeF<T, A, Allocator> > = BinaryTreeFFunctorMap<T, A, Allocator>{};
// 91fef612-39b5-4424-8ab5-7d8c80997e2c end

// ---------------------------------------------------------------------
// Elementwise layer fold (for fold_map).
// ---------------------------------------------------------------------

// 79aa4002-fa4a-472e-b01b-f580a13f60ec
/** Folds one BinaryTreeF layer elementwise, in order: the left child's
 * already-folded result, then the mapped node value, then the right
 * child's result. Absent children contribute the identity. In-order
 * traversal is this representation's contract; a non-commutative combine
 * observes it.
 */
struct BinaryTreeLayerFoldMap {
    template <typename MapFn, typename Combine, typename Result, typename T, typename Allocator>
    constexpr auto operator()(const MapFn&                             map_fn,
                              const Combine&                           combine,
                              const Result&                            identity,
                              const BinaryTreeF<T, Result, Allocator>& layer) const -> Result {
        Result left  = layer.left ? *layer.left : identity;
        Result right = layer.right ? *layer.right : identity;
        return combine(combine(left, map_fn(layer.value)), right);
    }
};

inline constexpr BinaryTreeLayerFoldMap binary_tree_layer_fold_map{};
// 79aa4002-fa4a-472e-b01b-f580a13f60ec end

// ---------------------------------------------------------------------
// Direct-verb projection, and the lookup registrations.
// ---------------------------------------------------------------------

// c9e514d9-4cf1-4fa1-ac4d-a9dfdf040291
/** Projection for the direct verbs: exposes one layer of a BinaryTree,
 * children as raw non-owning pointers read from the shared_ptr spine in
 * place — no conversion, no structure copied beyond one layer. The
 * pointer handles are complete types, so their slots are inline: the
 * projected layer allocates nothing. */
struct BinaryTreeProjectFn {
    template <typename T>
    auto operator()(const BinaryTree<T>* t) const -> BinaryTreeF<T, const BinaryTree<T>*> {
        using P = const BinaryTree<T>*;
        return BinaryTreeF<T, P>{t->value(),
                                 t->has_left() ? make_slot<P>(&t->left()) : child_slot_t<P>{},
                                 t->has_right() ? make_slot<P>(&t->right()) : child_slot_t<P>{}};
    }
};

inline constexpr BinaryTreeProjectFn binary_tree_project{};

/** Lookup registrations: the projection keyed on the tree type, the
 * layer fold keyed on the layer type. */
template <typename T>
inline constexpr auto project_typeclass<BinaryTree<T> > = binary_tree_project;

template <typename T, typename A, typename Allocator>
inline constexpr auto layer_fold_typeclass<BinaryTreeF<T, A, Allocator> > = binary_tree_layer_fold_map;
// c9e514d9-4cf1-4fa1-ac4d-a9dfdf040291 end

// ---------------------------------------------------------------------
// Conversions between the shared_ptr tree and its Fix form.
// ---------------------------------------------------------------------

// a7ed3ac1-4c61-4ae4-a633-f26216a39ac5
/** Convert a BinaryTree<T> to its Fix form, layer by layer. Deep copy: the
 * result owns its structure; the source tree is untouched. */
template <typename T>
auto to_fix(const BinaryTree<T>& tree) -> BinaryTreeFix<T> {
    using Fixed = BinaryTreeFix<T>;
    return wrap_fix<BinaryTreeLayer<T>::template F>(
        BinaryTreeF<T, Fixed>{tree.value(),
                              tree.has_left() ? make_slot<Fixed>(to_fix(tree.left())) : child_slot_t<Fixed>{},
                              tree.has_right() ? make_slot<Fixed>(to_fix(tree.right())) : child_slot_t<Fixed>{}});
}

/** Allocator-extended to_fix: converts a BinaryTree<T> to its Fix form
 * over @p Allocator, threading @p alloc into the tagged make_slot
 * (child_slot.hpp) at every knot Box (Decision 9) — the allocator is
 * for the Fix side, one knot Box per engaged child position, all built
 * on @p alloc's resource. Deep copy, same as the default-allocator
 * overload; recurses through this same overload so every level, not
 * just the root, is threaded. Tag-first, per Decision 9. */
template <typename T, typename Allocator>
auto to_fix(std::allocator_arg_t, const Allocator& alloc, const BinaryTree<T>& tree)
    -> Fix<BinaryTreeLayer<T, Allocator>::template F> {
    using Fixed = Fix<BinaryTreeLayer<T, Allocator>::template F>;
    return wrap_fix<BinaryTreeLayer<T, Allocator>::template F>(BinaryTreeF<T, Fixed, Allocator>{
        tree.value(),
        tree.has_left()
            ? make_slot<Fixed>(std::allocator_arg, alloc, to_fix(std::allocator_arg, alloc, tree.left()))
            : child_slot_t<Fixed, Allocator>{},
        tree.has_right()
            ? make_slot<Fixed>(std::allocator_arg, alloc, to_fix(std::allocator_arg, alloc, tree.right()))
            : child_slot_t<Fixed, Allocator>{}});
}

/** Extracts the element type from a BinaryTreeF layer type; T is a
 * non-deduced context inside BinaryTreeFix<T> (it sits behind a nested
 * alias template), so from_fix deduces the base functor and recovers the
 * element type through this trait instead. */
template <typename Layer>
struct binary_tree_element;

template <typename T, typename A, typename Allocator>
struct binary_tree_element<BinaryTreeF<T, A, Allocator> > {
    using type = T;
};

/** Rebuild a BinaryTree<T> from its Fix form — itself a fold: the algebra
 * reassembles one layer of BinaryTree from already-rebuilt children. */
template <template <typename> class F>
auto from_fix(const Fix<F>& fixed) -> BinaryTree<typename binary_tree_element<F<Fix<F> > >::type> {
    using T              = typename binary_tree_element<F<Fix<F> > >::type;
    auto rebuild_algebra = [](const F<BinaryTree<T> >& layer) -> BinaryTree<T> {
        return BinaryTree<T>::from_children_ptrs(layer.value,
                                                 layer.left ? BinaryTree<T>::make_ptr(*layer.left) : nullptr,
                                                 layer.right ? BinaryTree<T>::make_ptr(*layer.right) : nullptr);
    };
    return fold_fix<BinaryTree<T> >(rebuild_algebra, fixed);
}

/** Allocator-extended from_fix: rebuilds a BinaryTree<T> from its Fix
 * form, threading @p alloc into allocate_shared (via the tagged
 * make_ptr) for every reconstructed shared_ptr child (Decision 9) — the
 * allocator is for the rebuilt shared_ptr side. fold_fix itself needs
 * no allocator overload (D-A2 / Decision 9): the intermediate
 * F<BinaryTree<T>> layers it hands the algebra store children inline
 * (BinaryTree<T> is a complete type, not Fix), so no Box is ever built
 * during the fold — only the algebra's own allocate_shared calls
 * allocate, and those are threaded explicitly here. Tag-first, per
 * Decision 9. */
template <template <typename> class F, typename Allocator>
auto from_fix(std::allocator_arg_t, const Allocator& alloc, const Fix<F>& fixed)
    -> BinaryTree<typename binary_tree_element<F<Fix<F> > >::type> {
    using T              = typename binary_tree_element<F<Fix<F> > >::type;
    auto rebuild_algebra = [&alloc](const F<BinaryTree<T> >& layer) -> BinaryTree<T> {
        return BinaryTree<T>::from_children_ptrs(
            layer.value,
            layer.left ? BinaryTree<T>::make_ptr(std::allocator_arg, alloc, *layer.left) : nullptr,
            layer.right ? BinaryTree<T>::make_ptr(std::allocator_arg, alloc, *layer.right) : nullptr);
    };
    return fold_fix<BinaryTree<T> >(rebuild_algebra, fixed);
}
// a7ed3ac1-4c61-4ae4-a633-f26216a39ac5 end

} // namespace beman::tree_algorithms

#endif // BEMAN_TREE_ALGORITHMS_USE_MODULES() &&
       // !defined(BEMAN_TREE_ALGORITHMS_INCLUDED_FROM_INTERFACE_UNIT)

#endif // BEMAN_TREE_ALGORITHMS_BINARY_TREE_HPP
