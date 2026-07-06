// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/tree_algorithms/recursion_schemes.hpp>
#include <beman/tree_algorithms/recursion_schemes.hpp> // Re-inclusion: verifies include guard

#include <beman/tree_algorithms/box.hpp>
#include <beman/tree_algorithms/overloaded.hpp>

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <string>
#include <utility>
#include <variant>

using beman::tree_algorithms::Box;
using beman::tree_algorithms::Fix;
using beman::tree_algorithms::fold_fix;
using beman::tree_algorithms::make_box;
using beman::tree_algorithms::overloaded;
using beman::tree_algorithms::refold;
using beman::tree_algorithms::unfold_fix;
using beman::tree_algorithms::wrap_fix;

namespace {

// ---------------------------------------------------------------------
// NatF — unary naturals (ported from the fixpoint suite).
// ---------------------------------------------------------------------

struct Zero {};

template <typename A>
struct Succ {
    Box<A> pred;
};

template <typename A>
using NatF = std::variant<Zero, Succ<A>>;

using Nat = Fix<NatF>;

constexpr auto make_zero() -> Nat { return wrap_fix<NatF>(NatF<Nat>{Zero{}}); }

constexpr auto make_succ(Nat n) -> Nat { return wrap_fix<NatF>(NatF<Nat>{Succ<Nat>{make_box<Nat>(std::move(n))}}); }

template <typename A, typename F>
constexpr auto fmap_nat(F&& f, const NatF<A>& nat) {
    using B = std::remove_cvref_t<std::invoke_result_t<F, const A&>>;
    return std::visit(overloaded{
                          [](const Zero&) -> NatF<B> { return Zero{}; },
                          [&f](const Succ<A>& s) -> NatF<B> { return Succ<B>{make_box<B>(std::invoke(f, *s.pred))}; },
                      },
                      nat);
}

inline constexpr auto fmap_nat_fn = [](auto&& f, const auto& nat) {
    return fmap_nat(std::forward<decltype(f)>(f), nat);
};

inline constexpr auto count_algebra = [](const NatF<int>& n) -> int {
    return std::visit(overloaded{
                          [](const Zero&) { return 0; },
                          [](const Succ<int>& s) { return *s.pred + 1; },
                      },
                      n);
};

inline constexpr auto nat_coalgebra = [](int n) -> NatF<int> {
    if (n <= 0)
        return Zero{};
    return Succ<int>{make_box<int>(n - 1)};
};

// ---------------------------------------------------------------------
// IntListF — cons-lists of int, for the DEV-01 non-idempotent algebras.
// ---------------------------------------------------------------------

struct Nil {};

template <typename A>
struct Cons {
    int    head;
    Box<A> tail;
};

template <typename A>
using IntListF = std::variant<Nil, Cons<A>>;

using IntList = Fix<IntListF>;

template <typename A, typename F>
constexpr auto fmap_list(F&& f, const IntListF<A>& list) {
    using B = std::remove_cvref_t<std::invoke_result_t<F, const A&>>;
    return std::visit(
        overloaded{
            [](const Nil&) -> IntListF<B> { return Nil{}; },
            [&f](const Cons<A>& c) -> IntListF<B> { return Cons<B>{c.head, make_box<B>(std::invoke(f, *c.tail))}; },
        },
        list);
}

inline constexpr auto fmap_list_fn = [](auto&& f, const auto& list) {
    return fmap_list(std::forward<decltype(f)>(f), list);
};

constexpr auto make_nil() -> IntList { return wrap_fix<IntListF>(IntListF<IntList>{Nil{}}); }

constexpr auto make_cons(int head, IntList tail) -> IntList {
    return wrap_fix<IntListF>(IntListF<IntList>{Cons<IntList>{head, make_box<IntList>(std::move(tail))}});
}

// ---------------------------------------------------------------------
// TreeF — external binary tree, payload at leaves, for order-sensitive
// (non-commutative) fold and structure-sensitive refold tests.
// ---------------------------------------------------------------------

struct Leaf {
    int value;
};

template <typename A>
struct Node {
    Box<A> left;
    Box<A> right;
};

template <typename A>
using TreeF = std::variant<Leaf, Node<A>>;

using IntTree = Fix<TreeF>;

template <typename A, typename F>
constexpr auto fmap_tree(F&& f, const TreeF<A>& tree) {
    using B = std::remove_cvref_t<std::invoke_result_t<F, const A&>>;
    return std::visit(overloaded{
                          [](const Leaf& l) -> TreeF<B> { return Leaf{l.value}; },
                          [&f](const Node<A>& n) -> TreeF<B> {
                              return Node<B>{make_box<B>(std::invoke(f, *n.left)),
                                             make_box<B>(std::invoke(f, *n.right))};
                          },
                      },
                      tree);
}

inline constexpr auto fmap_tree_fn = [](auto&& f, const auto& tree) {
    return fmap_tree(std::forward<decltype(f)>(f), tree);
};

constexpr auto make_leaf(int v) -> IntTree { return wrap_fix<TreeF>(TreeF<IntTree>{Leaf{v}}); }

constexpr auto make_node(IntTree l, IntTree r) -> IntTree {
    return wrap_fix<TreeF>(
        TreeF<IntTree>{Node<IntTree>{make_box<IntTree>(std::move(l)), make_box<IntTree>(std::move(r))}});
}

// Non-commutative numeric algebra: leaf -> value, node -> left - right.
// Subtraction is neither commutative nor associative, so swapped children
// or re-associated structure produce a different answer.
inline constexpr auto subtract_algebra = [](const TreeF<int>& layer) -> int {
    return std::visit(overloaded{
                          [](const Leaf& l) { return l.value; },
                          [](const Node<int>& n) { return *n.left - *n.right; },
                      },
                      layer);
};

// Position-marking string algebra: the exact parenthesization records the
// shape and child order of the tree.
inline auto show_algebra = [](const TreeF<std::string>& layer) -> std::string {
    return std::visit(overloaded{
                          [](const Leaf& l) { return std::to_string(l.value); },
                          [](const Node<std::string>& n) { return "(" + *n.left + "-" + *n.right + ")"; },
                      },
                      layer);
};

// Range-splitting coalgebra: seed [lo, hi) becomes Leaf{lo} when the range
// is a single element, otherwise a Node splitting at the midpoint. The
// produced structure is position-sensitive: mis-threading either child seed
// changes the parenthesization (and the subtraction result).
using Range = std::pair<int, int>;

inline constexpr auto split_coalgebra = [](const Range& r) -> TreeF<Range> {
    auto [lo, hi] = r;
    if (hi - lo <= 1)
        return Leaf{lo};
    int mid = lo + (hi - lo) / 2;
    return Node<Range>{make_box<Range>(Range{lo, mid}), make_box<Range>(Range{mid, hi})};
};

// ---------------------------------------------------------------------
// Constexpr coverage (DEV-04): at least one static_assert per verb, each
// using an order-sensitive computation.
// ---------------------------------------------------------------------

// fold_fix: ((1 - 2) - 3) == -4; any child swap or re-association differs.
constexpr auto fold_subtracts_in_order() -> int {
    auto tree = make_node(make_node(make_leaf(1), make_leaf(2)), make_leaf(3));
    return fold_fix<int>(subtract_algebra, fmap_tree_fn, tree);
}

static_assert(fold_subtracts_in_order() == -4);

// unfold_fix: countdown-of-squares list from 3 is [9, 4, 1]; folded with the
// non-commutative combine h + 2 * rest: 9 + 2*(4 + 2*(1 + 2*0)) == 21.
// Wrong seed threading or wrong depth changes both the list and the total.
constexpr auto unfold_squares_then_weigh() -> int {
    auto squares_coalgebra = [](int n) -> IntListF<int> {
        if (n <= 0)
            return Nil{};
        return Cons<int>{n * n, make_box<int>(n - 1)};
    };
    auto weigh_algebra = [](const IntListF<int>& layer) -> int {
        return std::visit(overloaded{
                              [](const Nil&) { return 0; },
                              [](const Cons<int>& c) { return c.head + 2 * *c.tail; },
                          },
                          layer);
    };
    auto list = unfold_fix<IntListF>(squares_coalgebra, fmap_list_fn, 3);
    return fold_fix<int>(weigh_algebra, fmap_list_fn, list);
}

static_assert(unfold_squares_then_weigh() == 21);

// refold: split [1, 4) -> Node(Leaf 1, Node(Leaf 2, Leaf 3)); subtraction
// gives 1 - (2 - 3) == 2. Wrong split threading or wrong fusion order
// changes the association and hence the value.
constexpr auto refold_split_subtract() -> int {
    return refold<int, TreeF>(subtract_algebra, split_coalgebra, fmap_tree_fn, Range{1, 4});
}

static_assert(refold_split_subtract() == 2);

} // namespace

// ---------------------------------------------------------------------
// Ported fixpoint suite (NatF).
// ---------------------------------------------------------------------

TEST_CASE("FoldFix - NatZero", "[tree_algorithms::recursion_schemes]") {
    auto zero = make_zero();
    CHECK(fold_fix<int>(count_algebra, fmap_nat_fn, zero) == 0);
}

TEST_CASE("FoldFix - NatTwo", "[tree_algorithms::recursion_schemes]") {
    auto two = make_succ(make_succ(make_zero()));
    CHECK(fold_fix<int>(count_algebra, fmap_nat_fn, two) == 2);
}

TEST_CASE("FoldFix - NatFive", "[tree_algorithms::recursion_schemes]") {
    auto n = make_zero();
    for (int i = 0; i < 5; ++i) {
        n = make_succ(std::move(n));
    }
    CHECK(fold_fix<int>(count_algebra, fmap_nat_fn, n) == 5);
}

TEST_CASE("FoldFix - NatCustomAlgebra", "[tree_algorithms::recursion_schemes]") {
    auto three = make_succ(make_succ(make_succ(make_zero())));

    auto bool_algebra = [](const NatF<bool>& n) -> bool {
        return std::visit(overloaded{
                              [](const Zero&) { return true; },
                              [](const Succ<bool>& s) { return !*s.pred; },
                          },
                          n);
    };

    CHECK(fold_fix<bool>(bool_algebra, fmap_nat_fn, three) == false);
}

TEST_CASE("UnfoldFix - NatFromZero", "[tree_algorithms::recursion_schemes]") {
    auto nat = unfold_fix<NatF>(nat_coalgebra, fmap_nat_fn, 0);
    CHECK(fold_fix<int>(count_algebra, fmap_nat_fn, nat) == 0);
}

TEST_CASE("UnfoldFix - NatFromFive", "[tree_algorithms::recursion_schemes]") {
    auto nat = unfold_fix<NatF>(nat_coalgebra, fmap_nat_fn, 5);
    CHECK(fold_fix<int>(count_algebra, fmap_nat_fn, nat) == 5);
}

TEST_CASE("Refold - NatZero", "[tree_algorithms::recursion_schemes]") {
    CHECK(refold<int, NatF>(count_algebra, nat_coalgebra, fmap_nat_fn, 0) == 0);
}

TEST_CASE("Refold - NatFive", "[tree_algorithms::recursion_schemes]") {
    CHECK(refold<int, NatF>(count_algebra, nat_coalgebra, fmap_nat_fn, 5) == 5);
}

TEST_CASE("Refold - EquivalentToFoldOfUnfold", "[tree_algorithms::recursion_schemes]") {
    for (int n = 0; n < 10; ++n) {
        auto via_tree   = fold_fix<int>(count_algebra, fmap_nat_fn, unfold_fix<NatF>(nat_coalgebra, fmap_nat_fn, n));
        auto via_refold = refold<int, NatF>(count_algebra, nat_coalgebra, fmap_nat_fn, n);
        CHECK(via_tree == via_refold);
    }
}

// ---------------------------------------------------------------------
// DEV-01 discriminating tests: non-idempotent, order-sensitive algebras.
// ---------------------------------------------------------------------

TEST_CASE("FoldFix - OrderSensitiveStringAlgebra", "[tree_algorithms::recursion_schemes]") {
    // ((1-2)-3): the string records shape and child order exactly. If
    // fold_fix (or the fmap plumbing) visited children in the wrong order
    // the result would be, e.g., "((2-1)-3)" or "(3-(1-2))".
    auto tree = make_node(make_node(make_leaf(1), make_leaf(2)), make_leaf(3));
    CHECK(fold_fix<std::string>(show_algebra, fmap_tree_fn, tree) == "((1-2)-3)");

    // The mirrored tree must give the mirrored string, not the same one.
    auto mirrored = make_node(make_leaf(3), make_node(make_leaf(2), make_leaf(1)));
    CHECK(fold_fix<std::string>(show_algebra, fmap_tree_fn, mirrored) == "(3-(2-1))");
}

TEST_CASE("FoldFix - NonIdempotentDecrementAlgebra", "[tree_algorithms::recursion_schemes]") {
    // DEV-01's canonical discriminator: "decrement every head by 1" on
    // [10, 10, 10]. Correct answer 27; an implementation that applies the
    // algebra to a layer whose children were folded one extra or one fewer
    // time yields 28 or 26 instead.
    auto decrement_sum = [](const IntListF<int>& layer) -> int {
        return std::visit(overloaded{
                              [](const Nil&) { return 0; },
                              [](const Cons<int>& c) { return (c.head - 1) + *c.tail; },
                          },
                          layer);
    };
    auto list = make_cons(10, make_cons(10, make_cons(10, make_nil())));
    CHECK(fold_fix<int>(decrement_sum, fmap_list_fn, list) == 27);
}

TEST_CASE("UnfoldFix - PositionSensitiveCoalgebra", "[tree_algorithms::recursion_schemes]") {
    // Squares countdown from 4: [16, 9, 4, 1]. The head depends nonlinearly
    // on the seed and the tail on correct seed threading (n - 1), so a wrong
    // recursion depth or wrong child seed changes the serialized spine.
    auto squares_coalgebra = [](int n) -> IntListF<int> {
        if (n <= 0)
            return Nil{};
        return Cons<int>{n * n, make_box<int>(n - 1)};
    };
    auto show_list = [](const IntListF<std::string>& layer) -> std::string {
        return std::visit(overloaded{
                              [](const Nil&) -> std::string { return "end"; },
                              [](const Cons<std::string>& c) { return std::to_string(c.head) + ";" + *c.tail; },
                          },
                          layer);
    };
    auto list = unfold_fix<IntListF>(squares_coalgebra, fmap_list_fn, 4);
    CHECK(fold_fix<std::string>(show_list, fmap_list_fn, list) == "16;9;4;1;end");
}

TEST_CASE("Refold - FusionMatchesFoldOfUnfold", "[tree_algorithms::recursion_schemes]") {
    // Divide-and-conquer over [0, 5): the parenthesization records the exact
    // intermediate structure, so any divergence between the fused and the
    // materialized pipeline (wrong seed threading, wrong evaluation order)
    // shows up as a different string.
    Range seed{0, 5};
    auto  materialized =
        fold_fix<std::string>(show_algebra, fmap_tree_fn, unfold_fix<TreeF>(split_coalgebra, fmap_tree_fn, seed));
    auto fused = refold<std::string, TreeF>(show_algebra, split_coalgebra, fmap_tree_fn, seed);
    CHECK(fused == materialized);
    // Hand-computed: mid of [0,5) is 2, mid of [2,5) is 3.
    CHECK(fused == "((0-1)-(2-(3-4)))");

    // Non-commutative numeric check on the same structure:
    // (0-1) - (2 - (3-4)) == -1 - 3 == -4.
    CHECK(refold<int, TreeF>(subtract_algebra, split_coalgebra, fmap_tree_fn, seed) == -4);
    CHECK(fold_fix<int>(subtract_algebra, fmap_tree_fn, unfold_fix<TreeF>(split_coalgebra, fmap_tree_fn, seed)) == -4);
}
