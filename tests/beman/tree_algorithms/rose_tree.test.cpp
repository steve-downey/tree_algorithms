// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/tree_algorithms/rose_tree.hpp>
#include <beman/tree_algorithms/rose_tree.hpp> // Re-inclusion: verifies include guard

#include <beman/tree_algorithms/binary_tree.hpp>
#include <beman/tree_algorithms/fold_map.hpp>
#include <beman/tree_algorithms/fold_map_lookup.hpp>
#include <beman/tree_algorithms/recursion_schemes_lookup.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <memory>
#include <memory_resource>
#include <string>
#include <utility>
#include <vector>

using beman::tree_algorithms::BinaryTreeF;
using beman::tree_algorithms::BinaryTreeFix;
using beman::tree_algorithms::BinaryTreeLayer;
using beman::tree_algorithms::Box;
using beman::tree_algorithms::Fix;
using beman::tree_algorithms::fold_fix;
using beman::tree_algorithms::fold_map;
using beman::tree_algorithms::functor_typeclass;
using beman::tree_algorithms::has_functor_instance;
using beman::tree_algorithms::make_box;
using beman::tree_algorithms::rose;
using beman::tree_algorithms::rose_children_t;
using beman::tree_algorithms::rose_default_allocator;
using beman::tree_algorithms::rose_fmap;
using beman::tree_algorithms::rose_layer_fold_map;
using beman::tree_algorithms::RoseF;
using beman::tree_algorithms::RoseLayer;
using beman::tree_algorithms::RoseTreeFix;
using beman::tree_algorithms::unfold_fix;
using beman::tree_algorithms::unwrap_fix;
using beman::tree_algorithms::wrap_fix;

namespace {

using Tree = RoseTreeFix<int>;

// Order-and-shape-pinning algebra over one rose layer: a leaf renders
// its value, an internal node parenthesizes value-then-children. Both
// element order and nesting are observable (Decision 7 / DEV-01).
inline auto shape_algebra = [](const RoseF<int, std::string>& layer) -> std::string {
    if (layer.children.empty()) {
        return std::to_string(layer.value);
    }
    std::string out = "(" + std::to_string(layer.value);
    for (const std::string& child : layer.children) {
        out += " " + child;
    }
    return out + ")";
};

inline auto rose_shape(const Tree& t) -> std::string { return fold_fix<std::string>(shape_algebra, t); }

// ---------------------------------------------------------------------
// Constexpr coverage (DEV-04). The rose tree is Fix-native and vector is
// constexpr-capable in C++23 for transient allocations, so the whole
// lifecycle — build, fold — evaluates at compile time.
// ---------------------------------------------------------------------

static_assert(has_functor_instance<RoseF<int, int>>);

constexpr auto fmap_maps_children_in_order() -> bool {
    RoseF<int, int> layer{7, {1, 2, 3}};
    auto            mapped = functor_typeclass<RoseF<int, int>>.fmap([](const int& x) { return x * 2; }, layer);
    return mapped.value == 7 && mapped.children == std::vector<int>{2, 4, 6};
}

static_assert(fmap_maps_children_in_order());

constexpr auto layer_fold_is_preorder() -> bool {
    auto map_fn  = [](int x) { return x + 1; };
    auto combine = [](int a, int b) { return a * 10 + b; }; // order probe only, per-layer
    // Value first (mapped), then the already-folded child results, left
    // to right: map(1)=2, then 2, then 3.
    bool preorder = rose_layer_fold_map(map_fn, combine, -7, RoseF<int, int>{1, {2, 3}}) == 223;
    // A leaf is a node with no children: just its mapped value; the
    // identity never contributes.
    bool leaf = rose_layer_fold_map(map_fn, combine, -7, RoseF<int, int>{4, {}}) == 5;
    return preorder && leaf;
}

static_assert(layer_fold_is_preorder());

constexpr auto whole_lifecycle_at_compile_time() -> bool {
    Tree t       = rose<int>(1, {rose<int>(2), rose<int>(3, {rose<int>(4)})});
    auto combine = [](int a, int b) { return a * 10 + b; };
    // (1 (2) (3 (4))) pre-order under the digit probe: 3→34, then
    // 1→12→(12*10+34).
    return fold_map<int>([](int x) { return x; }, combine, 0, t) == 154;
}

static_assert(whole_lifecycle_at_compile_time());

// ---------------------------------------------------------------------
// Allocator-tagged spellings (WP-4), default-allocator path (DEV-04):
// rose(allocator_arg, ...) and rose_fmap(alloc) are new production code,
// so each gets its own constexpr round trip on rose_default_allocator,
// alongside the untagged spellings already covered above.
// ---------------------------------------------------------------------

constexpr auto allocator_tagged_rose_builds_and_folds() -> bool {
    // The tag-first overload with the default allocator must build
    // exactly the same shape and type as the untagged rose(): (1 (2)
    // (3)), pre-order digit-probed to 1 -> combine(1,2)=12 ->
    // combine(12,3)=123.
    Tree t       = rose(std::allocator_arg,
                  rose_default_allocator{},
                  1,
                  std::vector<Tree>{rose(std::allocator_arg, rose_default_allocator{}, 2),
                                          rose(std::allocator_arg, rose_default_allocator{}, 3)});
    auto combine = [](int a, int b) { return a * 10 + b; };
    return fold_map<int>([](int x) { return x; }, combine, 0, t) == 123;
}

static_assert(allocator_tagged_rose_builds_and_folds());

constexpr auto rose_fmap_builds_fanout_at_compile_time() -> bool {
    // Same fanout-shrinking coalgebra as UnfoldGrowsFanout below, but
    // driven through the explicit-fmap unfold_fix with rose_fmap(alloc)
    // instead of the lookup tier, over rose_default_allocator.
    using Seed     = std::pair<int, int>;
    auto coalgebra = [](const Seed& s) -> RoseF<int, Seed, rose_default_allocator> {
        auto [label, fanout] = s;
        rose_children_t<Seed, rose_default_allocator> children;
        children.reserve(static_cast<std::size_t>(fanout));
        for (int i = 1; i <= fanout; ++i) {
            children.push_back(Seed{label * 10 + i, fanout - 1});
        }
        return RoseF<int, Seed, rose_default_allocator>{label, std::move(children)};
    };

    auto t = unfold_fix<RoseLayer<int, rose_default_allocator>::template F>(
        coalgebra, rose_fmap(rose_default_allocator{}), Seed{1, 2});

    // (1 (11 (111)) (12 (121))) pre-order digit-probed: 11->221, 12->241,
    // then 1 -> 231 -> 2551. A wrong traversal order or arity gives a
    // different composite (DEV-01).
    auto combine = [](int a, int b) { return a * 10 + b; };
    return fold_map<int>([](int x) { return x; }, combine, 0, t) == 2551;
}

static_assert(rose_fmap_builds_fanout_at_compile_time());

} // namespace

// ---------------------------------------------------------------------
// The verbs over the Fix-native rose tree.
// ---------------------------------------------------------------------

TEST_CASE("RoseTree - FoldPinsShapeAndOrder", "[tree_algorithms::rose_tree]") {
    auto t = rose<int>(1, {rose<int>(2), rose<int>(3, {rose<int>(4)}), rose<int>(5)});
    CHECK(rose_shape(t) == "(1 2 (3 4) 5)");

    // Reordered children are a different tree and render differently.
    auto swapped = rose<int>(1, {rose<int>(3, {rose<int>(4)}), rose<int>(2), rose<int>(5)});
    CHECK(rose_shape(swapped) == "(1 (3 4) 2 5)");
}

TEST_CASE("RoseTree - UnfoldGrowsFanout", "[tree_algorithms::rose_tree]") {
    // Seed (label, fanout): a node numbers its children label*10+i and
    // shrinks the fanout — labels pin both order and depth.
    using Seed     = std::pair<int, int>;
    auto coalgebra = [](const Seed& s) -> RoseF<int, Seed> {
        auto [label, fanout] = s;
        std::vector<Seed> children;
        children.reserve(static_cast<std::size_t>(fanout));
        for (int i = 1; i <= fanout; ++i) {
            children.push_back(Seed{label * 10 + i, fanout - 1});
        }
        return RoseF<int, Seed>{label, std::move(children)};
    };

    auto t = unfold_fix<RoseLayer<int>::template F>(coalgebra, Seed{1, 2});
    CHECK(rose_shape(t) == "(1 (11 111) (12 121))");
}

// ---------------------------------------------------------------------
// fold_map over the rose tree, and the equivalence law pinning the
// lookup tier to the explicit tier.
// ---------------------------------------------------------------------

TEST_CASE("RoseTree - FoldMapPreorderContract", "[tree_algorithms::rose_tree]") {
    auto t = rose<int>(1, {rose<int>(2), rose<int>(3, {rose<int>(4)}), rose<int>(5)});

    auto show   = [](int x) { return std::to_string(x); };
    auto concat = [](const std::string& a, const std::string& b) { return a + b; };

    // DEV-01: string concatenation is non-commutative, so the pre-order
    // contract is observed, not assumed.
    CHECK(fold_map<std::string>(show, concat, std::string{}, t) == "12345");

    auto swapped = rose<int>(1, {rose<int>(3, {rose<int>(4)}), rose<int>(2), rose<int>(5)});
    CHECK(fold_map<std::string>(show, concat, std::string{}, swapped) == "13425");

    // Equivalence law: the lookup tier IS the explicit tier with the
    // registered ingredients — same answer through both spellings.
    const auto& fmap_fn = [](auto&& fn, const auto& layer) {
        using Layer = std::remove_cvref_t<decltype(layer)>;
        return functor_typeclass<Layer>.fmap(std::forward<decltype(fn)>(fn), layer);
    };
    CHECK(fold_map<std::string>(show, concat, std::string{}, rose_layer_fold_map, fmap_fn, t) ==
          fold_map<std::string>(show, concat, std::string{}, t));

    // Derived one-liners.
    auto plus = [](int a, int b) { return a + b; };
    CHECK(fold_map<int>([](int) { return 1; }, plus, 0, t) == 5);
    CHECK(fold_map<int>([](int x) { return x; }, plus, 0, t) == 15);
}

// ---------------------------------------------------------------------
// There is no generic in-order: the same fold_map call, two
// representations, two contractual orders.
// ---------------------------------------------------------------------

TEST_CASE("RoseTree - InOrderIsBinaryNotGeneric", "[tree_algorithms::rose_tree]") {
    auto show   = [](int x) { return std::to_string(x); };
    auto concat = [](const std::string& a, const std::string& b) { return a + b; };

    // The same three values, 2 above 1 and 3, in both representations.
    using BinTree = BinaryTreeFix<int>;
    auto bin_leaf = [](int v) -> BinTree {
        return wrap_fix<BinaryTreeLayer<int>::template F>(
            BinaryTreeF<int, BinTree>{v, Box<BinTree>{}, Box<BinTree>{}});
    };
    auto bin = wrap_fix<BinaryTreeLayer<int>::template F>(
        BinaryTreeF<int, BinTree>{2, make_box<BinTree>(bin_leaf(1)), make_box<BinTree>(bin_leaf(3))});

    auto rosy = rose<int>(2, {rose<int>(1), rose<int>(3)});

    // The binary layer's contract is in-order — the value sits between
    // its two children. The rose layer has no such slot (which child
    // pair would the value sit between?), so its contract is pre-order.
    // Neither is wrong, and no generic in-order exists to impose.
    CHECK(fold_map<std::string>(show, concat, std::string{}, bin) == "123");
    CHECK(fold_map<std::string>(show, concat, std::string{}, rosy) == "213");
}

// ---------------------------------------------------------------------
// WP-4 gate: a pmr rose tree, built two ways, with every children vector
// at every level proved to have allocated from the supplied resource. A
// std::pmr::monotonic_buffer_resource over a null_memory_resource
// upstream: any allocation that escaped the buffer would throw, so a
// completed build+fold is itself proof of zero leakage.
// ---------------------------------------------------------------------

TEST_CASE("RoseTree - PmrRoseRoutesEveryLevelThroughTheResource", "[tree_algorithms::rose_tree]") {
    using PA       = std::pmr::polymorphic_allocator<std::byte>;
    using PmrTree  = Fix<RoseLayer<int, PA>::template F>;
    using ChildVec = rose_children_t<PmrTree, PA>;

    alignas(std::max_align_t) unsigned char buffer[16 * 1024];
    std::pmr::monotonic_buffer_resource     pool(buffer, sizeof(buffer), std::pmr::null_memory_resource());
    PA                                      a(&pool);

    // Build bottom-up with the allocator-tagged rose(): (1 2 (3 4) 5).
    // rose() re-homes whatever children vector it is handed onto `a`, so
    // it does not matter that the scratch ChildVecs below start out on a
    // default-constructed allocator — BUT the already-built subtrees
    // must be MOVED into them, never brace-init-copied: PmrTree isn't
    // itself allocator-aware, so copying one copies its nested children
    // vector too, and polymorphic_allocator's
    // select_on_container_copy_construction deliberately resets a copy
    // to the default resource (the container rules Decision 9 commits
    // to) — correct semantics for "a copy starts fresh", but it would
    // silently strand the copy's children off the pool if this test
    // brace-initialized ChildVec{n2, n3, n5} instead of push_back'ing
    // moves.
    auto     n4 = rose(std::allocator_arg, a, 4);
    ChildVec n3_children;
    n3_children.push_back(std::move(n4));
    auto n3 = rose(std::allocator_arg, a, 3, std::move(n3_children));

    auto     n2 = rose(std::allocator_arg, a, 2);
    auto     n5 = rose(std::allocator_arg, a, 5);
    ChildVec top_children;
    top_children.push_back(std::move(n2));
    top_children.push_back(std::move(n3));
    top_children.push_back(std::move(n5));
    auto t = rose(std::allocator_arg, a, 1, std::move(top_children));

    // Every level's children vector is on our pool: the root, and the
    // grandchild level under node 3.
    const auto& root = unwrap_fix(t);
    CHECK(root.children.get_allocator().resource() == &pool);
    const auto& node3 = unwrap_fix(root.children[1]);
    CHECK(node3.value == 3);
    CHECK(node3.children.get_allocator().resource() == &pool);

    // DEV-01: non-commutative string concatenation pins both order and
    // shape, resolved through the generalized functor_typeclass/
    // layer_fold_typeclass registrations (lookup tier) over the pmr
    // layer shape — proving the WP-4 generalization is wired for pmr,
    // not just for the default allocator.
    auto show   = [](int x) { return std::to_string(x); };
    auto concat = [](const std::string& a, const std::string& b) { return a + b; };
    CHECK(fold_map<std::string>(show, concat, std::string{}, t) == "12345");
}

TEST_CASE("RoseTree - PmrUnfoldBuildsEntirelyFromThePool", "[tree_algorithms::rose_tree]") {
    using PA   = std::pmr::polymorphic_allocator<std::byte>;
    using Seed = std::pair<int, int>;

    alignas(std::max_align_t) unsigned char buffer[16 * 1024];
    std::pmr::monotonic_buffer_resource     pool(buffer, sizeof(buffer), std::pmr::null_memory_resource());
    PA                                      a(&pool);

    // Same fanout-shrinking coalgebra as the compile-time rose_fmap
    // test: the coalgebra's own (Seed-typed) children vector needs no
    // resource of its own — it is a throwaway intermediate that
    // rose_fmap(a) consumes, and it is rose_fmap(a) alone that decides
    // the allocator of the Fix<F> children vector it builds.
    auto coalgebra = [](const Seed& s) -> RoseF<int, Seed, PA> {
        auto [label, fanout] = s;
        rose_children_t<Seed, PA> children;
        children.reserve(static_cast<std::size_t>(fanout));
        for (int i = 1; i <= fanout; ++i) {
            children.push_back(Seed{label * 10 + i, fanout - 1});
        }
        return RoseF<int, Seed, PA>{label, std::move(children)};
    };

    // Built over the pool via the EXISTING unfold_fix + rose_fmap(a) —
    // no verb-level allocator overload, matching the expression tree's
    // pattern (Decision 9, 2026-07-18 amendment).
    auto t = unfold_fix<RoseLayer<int, PA>::template F>(coalgebra, rose_fmap(a), Seed{1, 2});

    const auto& root = unwrap_fix(t);
    CHECK(root.children.get_allocator().resource() == &pool);
    const auto& node11 = unwrap_fix(root.children[0]);
    CHECK(node11.value == 11);
    CHECK(node11.children.get_allocator().resource() == &pool);

    // That the build completed at all (no throw from the null upstream)
    // proves no allocation escaped the pool. Non-idempotent numeric
    // check pins shape and order (DEV-01), matching the compile-time
    // rose_fmap test's expectation.
    auto combine = [](int x, int y) { return x * 10 + y; };
    CHECK(fold_map<int>([](int x) { return x; }, combine, 0, t) == 2551);
}
