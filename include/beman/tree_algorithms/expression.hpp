// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_TREE_ALGORITHMS_EXPRESSION_HPP
#define BEMAN_TREE_ALGORITHMS_EXPRESSION_HPP

#include <beman/tree_algorithms/config.hpp>

#if BEMAN_TREE_ALGORITHMS_USE_MODULES() && !defined(BEMAN_TREE_ALGORITHMS_INCLUDED_FROM_INTERFACE_UNIT)

import beman.tree_algorithms;

#else

    #include <beman/tree_algorithms/box.hpp>
    #include <beman/tree_algorithms/child_slot.hpp>
    #include <beman/tree_algorithms/fix.hpp>
    #include <beman/tree_algorithms/fold_map_lookup.hpp>
    #include <beman/tree_algorithms/functor.hpp>
    #include <beman/tree_algorithms/overloaded.hpp>
    #include <beman/tree_algorithms/recursion_schemes_lookup.hpp>

    #if !BEMAN_TREE_ALGORITHMS_USE_MODULES()
        #include <functional>
        #include <type_traits>
        #include <utility>
        #include <variant>
    #endif

namespace beman::tree_algorithms {

// The fixpoint expression tree: the simplest recursive datatype in the
// library, used throughout the paper and tests to demonstrate the pattern.
// Layout is types -> functor_typeclass instance -> smart constructors ->
// algebras: the functor_typeclass partial specialization must be visible
// before any function in this header that dispatches through the lookup
// verbs for ExprF layers.

// ---------------------------------------------------------------------
// The non-recursive base functor.
// ---------------------------------------------------------------------

// 085bb189-a48e-4262-aefd-b64f8755e959
/** The allocator a layer's Box-at-knot children default to. A fixed
 * (value-type-independent) spelling so the one-argument alternative
 * spellings — Add<A>, Mul<A> — name exactly the same type the default
 * ExprF variant holds; child_slot rebinds it to Fix at the knot, so this
 * is Box<Fix, std::allocator<Fix>> as before (Decision 9, 2026-07-18
 * amendment). */
using expr_default_allocator = std::allocator<std::byte>;

/** Constant leaf alternative; carries an int literal and no recursive
 * positions, so it needs no allocator parameter.
 * @tparam A recursive position placeholder (not yet fixed)
 */
template <typename A>
struct Const {
    int val;
};

/** Addition node alternative; left and right sub-expressions in child
 * slots — boxed at the knot, inline at complete types. Parameterized on
 * the allocator so a stateful (pmr) allocator can live in the knot Box;
 * the default is the stateless std::allocator, unchanged.
 * @tparam A recursive position placeholder
 * @tparam Allocator allocator carried at the knot */
template <typename A, typename Allocator = expr_default_allocator>
struct Add {
    child_slot_t<A, Allocator> left, right;
};

/** Multiplication node alternative; left and right sub-expressions in
 * child slots. */
template <typename A, typename Allocator = expr_default_allocator>
struct Mul {
    child_slot_t<A, Allocator> left, right;
};

/** Binds the allocator of an expression layer, leaving the unary-in-A
 * alias template @c F that Fix and the verbs require (à la RoseLayer<T>). */
template <typename Allocator>
struct ExprLayer {
    template <typename A>
    using F = std::variant<Const<A>, Add<A, Allocator>, Mul<A, Allocator> >;
};

/** Non-recursive expression base functor: one layer of an expression tree
 * with @p A filling the recursive positions, over the default (stateless)
 * allocator. Expr = Fix<ExprF> ties the knot. A pmr expression tree is
 * Fix<ExprLayer<pmr-allocator>::template F>.
 */
template <typename A>
using ExprF = ExprLayer<expr_default_allocator>::template F<A>;
// 085bb189-a48e-4262-aefd-b64f8755e959 end

// ---------------------------------------------------------------------
// Functor instance.
// ---------------------------------------------------------------------

// a052ddcb-f05b-41c9-85bf-4c443ab438b7
/** Functor primitive for an expression layer over @p Allocator: applies
 * @p fn at each recursive position of one layer. Constants have no
 * recursive positions, so the value copies across untouched. This
 * primitive boxes with the default allocator, which is correct for the
 * default (stateless) tree and for any fold (fold layers store children
 * inline, so the allocator type never reaches a Box). Unfolding a stateful
 * (pmr) tree needs the resource value, which no fixed lookup object can
 * carry — use expr_fmap(alloc) with the explicit-fmap verbs for that.
 */
template <typename A, typename Allocator>
struct ExprFFunctorImpl {
    using Layer = typename ExprLayer<Allocator>::template F<A>;
    template <typename B>
    using LayerB = typename ExprLayer<Allocator>::template F<B>;

    template <typename Fn>
    constexpr auto fmap(this auto&&, Fn&& fn, const Layer& layer) {
        using B = std::remove_cvref_t<std::invoke_result_t<Fn, const A&> >;
        return std::visit(
            overloaded{
                [](const Const<A>& c) -> LayerB<B> { return Const<B>{c.val}; },
                [&fn](const Add<A, Allocator>& a) -> LayerB<B> {
                    return Add<B, Allocator>{make_slot<B>(std::invoke(fn, *a.left)),
                                             make_slot<B>(std::invoke(fn, *a.right))};
                },
                [&fn](const Mul<A, Allocator>& m) -> LayerB<B> {
                    return Mul<B, Allocator>{make_slot<B>(std::invoke(fn, *m.left)),
                                             make_slot<B>(std::invoke(fn, *m.right))};
                },
            },
            layer);
    }
};

/** Functor map for an expression layer: the fmap primitive plus the
 * derived operations from the Functor CRTP base. */
template <typename A, typename Allocator>
struct ExprFFunctorMap : Functor<ExprFFunctorImpl<A, Allocator> > {
    using ExprFFunctorImpl<A, Allocator>::fmap;
};

/** Registers ExprFFunctorMap as the Functor instance for every expression
 * layer, over any allocator (keyed on the variant shape). */
template <typename A, typename Allocator>
inline constexpr auto functor_typeclass<std::variant<Const<A>, Add<A, Allocator>, Mul<A, Allocator> > > =
    ExprFFunctorMap<A, Allocator>{};

/** Allocator-carrying fmap for expression layers: identical in shape to
 * the functor primitive, but it threads a captured allocator VALUE into
 * the tagged make_slot it boxes with. Passing expr_fmap(alloc) to the
 * explicit-fmap unfold_fix/refold puts every node of the built tree on
 * @p alloc's resource — the mechanism by which the verbs are allocator-
 * aware without an allocator parameter (Decision 9, 2026-07-18). */
template <typename Allocator>
struct ExprFFmapAlloc {
    Allocator alloc;

    template <typename Fn, typename A>
    constexpr auto operator()(Fn&& fn, const typename ExprLayer<Allocator>::template F<A>& layer) const {
        using B      = std::remove_cvref_t<std::invoke_result_t<Fn, const A&> >;
        using LayerB = typename ExprLayer<Allocator>::template F<B>;
        return std::visit(
            overloaded{
                [](const Const<A>& c) -> LayerB { return Const<B>{c.val}; },
                [this, &fn](const Add<A, Allocator>& a) -> LayerB {
                    return Add<B, Allocator>{make_slot<B>(std::allocator_arg, alloc, std::invoke(fn, *a.left)),
                                             make_slot<B>(std::allocator_arg, alloc, std::invoke(fn, *a.right))};
                },
                [this, &fn](const Mul<A, Allocator>& m) -> LayerB {
                    return Mul<B, Allocator>{make_slot<B>(std::allocator_arg, alloc, std::invoke(fn, *m.left)),
                                             make_slot<B>(std::allocator_arg, alloc, std::invoke(fn, *m.right))};
                },
            },
            layer);
    }
};

/** Build an allocator-carrying fmap for expression layers over @p alloc. */
template <typename Allocator>
constexpr auto expr_fmap(const Allocator& alloc) -> ExprFFmapAlloc<Allocator> {
    return ExprFFmapAlloc<Allocator>{alloc};
}
// a052ddcb-f05b-41c9-85bf-4c443ab438b7 end

// ---------------------------------------------------------------------
// Smart constructors.
// ---------------------------------------------------------------------

// d0f9b5a2-363f-49ca-9a37-bfb0e6c9ff50
/** The fixed-point expression tree type. */
using Expr = Fix<ExprF>;

/** Build a constant leaf holding @p v. */
constexpr auto const_node(int v) -> Expr { return wrap_fix<ExprF>(ExprF<Expr>{Const<Expr>{v}}); }

/** Build @p l + @p r. */
constexpr auto add_node(Expr l, Expr r) -> Expr {
    return wrap_fix<ExprF>(ExprF<Expr>{Add<Expr>{make_slot<Expr>(std::move(l)), make_slot<Expr>(std::move(r))}});
}

/** Build @p l * @p r. */
constexpr auto mul_node(Expr l, Expr r) -> Expr {
    return wrap_fix<ExprF>(ExprF<Expr>{Mul<Expr>{make_slot<Expr>(std::move(l)), make_slot<Expr>(std::move(r))}});
}
// d0f9b5a2-363f-49ca-9a37-bfb0e6c9ff50 end

// ---------------------------------------------------------------------
// Evaluation.
// ---------------------------------------------------------------------

// bbf73c10-b63e-434a-91f1-d1a3a7c23b96
/** Evaluation algebra: reduces one already-evaluated ExprF layer to an
 * int. Not recursive; fold_fix supplies the recursion.
 */
inline constexpr auto eval_algebra = [](const ExprF<int>& expr) -> int {
    return std::visit(overloaded{
                          [](const Const<int>& c) { return c.val; },
                          [](const Add<int>& a) { return *a.left + *a.right; },
                          [](const Mul<int>& m) { return *m.left * *m.right; },
                      },
                      expr);
};
// bbf73c10-b63e-434a-91f1-d1a3a7c23b96 end

// ---------------------------------------------------------------------
// Elementwise layer fold (for fold_map).
// ---------------------------------------------------------------------

// 8f25579d-443a-4685-b3af-01f8103f6863
/** Folds one ExprF layer elementwise: the elements of an expression tree
 * are the constants, so Const maps its value and Add/Mul just combine
 * their children's results. Note the contrast with eval_algebra: an
 * elementwise fold sees the constants through one combine, where
 * evaluation interprets each alternative — summing the constants of
 * (1 + 2) * 3 gives 6, evaluating it gives 9. ExprF has no empty
 * alternative, so the identity goes unused.
 */
struct ExprLayerFoldMap {
    template <typename MapFn, typename Combine, typename Result, typename Allocator>
    constexpr auto operator()(const MapFn&                                                                map_fn,
                              const Combine&                                                              combine,
                              const Result& /*identity*/,
                              const std::variant<Const<Result>, Add<Result, Allocator>, Mul<Result, Allocator> >& layer)
        const -> Result {
        return std::visit(overloaded{
                              [&](const Const<Result>& c) -> Result { return map_fn(c.val); },
                              [&](const Add<Result, Allocator>& a) -> Result { return combine(*a.left, *a.right); },
                              [&](const Mul<Result, Allocator>& m) -> Result { return combine(*m.left, *m.right); },
                          },
                          layer);
    }
};

inline constexpr ExprLayerFoldMap expr_layer_fold_map{};

/** Lookup registration for the expression layer fold, over any allocator. */
template <typename A, typename Allocator>
inline constexpr auto layer_fold_typeclass<std::variant<Const<A>, Add<A, Allocator>, Mul<A, Allocator> > > =
    expr_layer_fold_map;
// 8f25579d-443a-4685-b3af-01f8103f6863 end

// 66235297-8e2a-4610-b6a4-a3f2a8837fb0
/** Evaluate an expression tree bottom-up in one fold, resolving fmap
 * through the functor_typeclass instance above. */
constexpr auto eval(const Expr& expr) -> int { return fold_fix<int>(eval_algebra, expr); }
// 66235297-8e2a-4610-b6a4-a3f2a8837fb0 end

} // namespace beman::tree_algorithms

#endif // BEMAN_TREE_ALGORITHMS_USE_MODULES() &&
       // !defined(BEMAN_TREE_ALGORITHMS_INCLUDED_FROM_INTERFACE_UNIT)

#endif // BEMAN_TREE_ALGORITHMS_EXPRESSION_HPP
