// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_TREE_ALGORITHMS_RECURSION_SCHEMES_HPP
#define BEMAN_TREE_ALGORITHMS_RECURSION_SCHEMES_HPP

#include <beman/tree_algorithms/config.hpp>

#if BEMAN_TREE_ALGORITHMS_USE_MODULES() && !defined(BEMAN_TREE_ALGORITHMS_INCLUDED_FROM_INTERFACE_UNIT)

import beman.tree_algorithms;

#else

    #include <beman/tree_algorithms/fix.hpp>

    #if !BEMAN_TREE_ALGORITHMS_USE_MODULES()
        #include <utility>
    #endif

namespace beman::tree_algorithms {

// The three recursion verbs over Fix<F>. Each takes the base functor's fmap
// explicitly as a callable parameter; result carriers are leading explicit
// template parameters because C++ cannot infer fold carriers through
// recursive calls.
//
// Lookup-based overloads (which resolve fmap through functor_typeclass
// lookup) live in recursion_schemes_lookup.hpp (Decision 4); this header has no
// typeclass dependency.

/** Recursive fold (reduce): collapse a Fix<F> tree bottom-up into a Result.
 *
 * Unwraps one layer, recursively folds every child position via @p fmap_fn,
 * then combines the fully evaluated layer with @p algebra.
 * @tparam Result the fold carrier; must be given explicitly
 * @param algebra  callable F<Result> -> Result
 * @param fmap_fn  callable (Fn, const F<A>&) -> F<B> — the functor's fmap
 * @param tree     the fixed-point value to fold
 */
template <typename Result, template <typename> class F, typename Algebra, typename FMap>
constexpr auto fold_fix(const Algebra& algebra, const FMap& fmap_fn, const Fix<F>& tree) -> Result {
    const auto& layer = unwrap_fix(tree);
    auto        evaluated =
        fmap_fn([&](const Fix<F>& child) -> Result { return fold_fix<Result>(algebra, fmap_fn, child); }, layer);
    return algebra(evaluated);
}

/** Recursive build (unfold): grow a Fix<F> tree top-down from a seed.
 *
 * Expands @p seed one layer with @p coalgebra, recursively unfolds every
 * child seed via @p fmap_fn, then wraps the completed layer.
 * @tparam F the base functor; must be given explicitly
 * @param coalgebra callable Seed -> F<Seed>
 * @param fmap_fn   callable (Fn, const F<A>&) -> F<B> — the functor's fmap
 * @param seed      the starting seed
 */
template <template <typename> class F, typename Coalgebra, typename FMap, typename Seed>
constexpr auto unfold_fix(const Coalgebra& coalgebra, const FMap& fmap_fn, const Seed& seed) -> Fix<F> {
    auto layer = coalgebra(seed);
    auto expanded =
        fmap_fn([&](const Seed& child) -> Fix<F> { return unfold_fix<F>(coalgebra, fmap_fn, child); }, layer);
    return wrap_fix<F>(std::move(expanded));
}

/** Fused unfold-then-fold: compute fold_fix(algebra, unfold_fix(coalgebra, seed))
 * without materializing the intermediate Fix<F> tree.
 *
 * Each layer produced by @p coalgebra is consumed by @p algebra as soon as
 * its children have been recursively refolded.
 * @tparam Result the fold carrier; must be given explicitly
 * @tparam F      the base functor; must be given explicitly
 */
template <typename Result,
          template <typename> class F,
          typename Algebra,
          typename Coalgebra,
          typename FMap,
          typename Seed>
constexpr auto refold(const Algebra& algebra, const Coalgebra& coalgebra, const FMap& fmap_fn, const Seed& seed)
    -> Result {
    auto layer     = coalgebra(seed);
    auto evaluated = fmap_fn(
        [&](const Seed& child) -> Result { return refold<Result, F>(algebra, coalgebra, fmap_fn, child); }, layer);
    return algebra(evaluated);
}

} // namespace beman::tree_algorithms

#endif // BEMAN_TREE_ALGORITHMS_USE_MODULES() &&
       // !defined(BEMAN_TREE_ALGORITHMS_INCLUDED_FROM_INTERFACE_UNIT)

#endif // BEMAN_TREE_ALGORITHMS_RECURSION_SCHEMES_HPP
