// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_TREE_ALGORITHMS_FIX_HPP
#define BEMAN_TREE_ALGORITHMS_FIX_HPP

#include <beman/tree_algorithms/config.hpp>

#if BEMAN_TREE_ALGORITHMS_USE_MODULES() && !defined(BEMAN_TREE_ALGORITHMS_INCLUDED_FROM_INTERFACE_UNIT)

import beman.tree_algorithms;

#else

    #if !BEMAN_TREE_ALGORITHMS_USE_MODULES()
        #include <utility>
    #endif

namespace beman::tree_algorithms {

/** Fixed-point combinator that ties the recursive knot for a functor @p F.
 *
 * Fix<F> is the iso-recursive type satisfying Fix<F> ≅ F<Fix<F>>.
 * The single data member @c inner holds one unwrapped layer; wrap/unwrap
 * are the iso-recursive isomorphism boundary.
 * Use Box<Fix<F>> inside F to avoid infinite template instantiation depth.
 * @tparam F unary template functor (takes the recursive position as its param)
 */
// 57d4bd6e-c8c7-4806-afd3-2e42aec8ae27
template <template <typename> class F>
struct Fix {
    F<Fix<F>> inner;
};
// 57d4bd6e-c8c7-4806-afd3-2e42aec8ae27 end

// 53775b7e-8a78-4b79-885b-046f6232d7a3
/** Wrap one layer of @p F into the fixed-point type. */
template <template <typename> class F>
constexpr auto wrap_fix(F<Fix<F>> layer) -> Fix<F> {
    return Fix<F>{std::move(layer)};
}

/** Unwrap one layer from a fixed-point value, exposing F<Fix<F>>. */
template <template <typename> class F>
constexpr auto unwrap_fix(const Fix<F>& fixed) -> const F<Fix<F>>& {
    return fixed.inner;
}
// 53775b7e-8a78-4b79-885b-046f6232d7a3 end

} // namespace beman::tree_algorithms

#endif // BEMAN_TREE_ALGORITHMS_USE_MODULES() &&
       // !defined(BEMAN_TREE_ALGORITHMS_INCLUDED_FROM_INTERFACE_UNIT)

#endif // BEMAN_TREE_ALGORITHMS_FIX_HPP
