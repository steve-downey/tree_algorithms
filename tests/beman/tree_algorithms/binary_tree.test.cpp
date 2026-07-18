// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/tree_algorithms/binary_tree.hpp>
#include <beman/tree_algorithms/binary_tree.hpp> // Re-inclusion: verifies include guard

#include <beman/tree_algorithms/box.hpp>
#include <beman/tree_algorithms/recursion_schemes.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <memory_resource>
#include <string>
#include <type_traits>
#include <utility>

using beman::tree_algorithms::BinaryTree;
using beman::tree_algorithms::binary_tree_default_allocator;
using beman::tree_algorithms::BinaryTreeF;
using beman::tree_algorithms::BinaryTreeFix;
using beman::tree_algorithms::BinaryTreeLayer;
using beman::tree_algorithms::Box;
using beman::tree_algorithms::child_slot_t;
using beman::tree_algorithms::Fix;
using beman::tree_algorithms::fold_fix;
using beman::tree_algorithms::fold_with;
using beman::tree_algorithms::from_fix;
using beman::tree_algorithms::functor_typeclass;
using beman::tree_algorithms::has_functor_instance;
using beman::tree_algorithms::make_box;
using beman::tree_algorithms::make_slot;
using beman::tree_algorithms::refold;
using beman::tree_algorithms::to_fix;
using beman::tree_algorithms::unfold_fix;
using beman::tree_algorithms::unwrap_fix;
using beman::tree_algorithms::wrap_fix;

namespace {

// The shared_ptr BinaryTree is adapted to the verbs through BinaryTreeF +
// to_fix/from_fix; BinaryTree itself is not constexpr-capable (shared_ptr
// allocation), so the compile-time coverage below runs on the Fix side,
// which is fully constexpr. That split is expected and documented in the
// header.

using IntTree  = BinaryTree<int>;
using IntFixed = BinaryTreeFix<int>;

// Constexpr builders for the Fix form (DEV-03: full types, no bare CTAD).
constexpr auto fixed_leaf(int v) -> IntFixed {
    return wrap_fix<BinaryTreeLayer<int>::F>(BinaryTreeF<int, IntFixed>{v, Box<IntFixed>{}, Box<IntFixed>{}});
}

constexpr auto fixed_node(int v, IntFixed l, IntFixed r) -> IntFixed {
    return wrap_fix<BinaryTreeLayer<int>::F>(
        BinaryTreeF<int, IntFixed>{v, make_box<IntFixed>(std::move(l)), make_box<IntFixed>(std::move(r))});
}

// Hand-rolled explicit fmap, independent of the header's functor instance.
template <typename A, typename Fn>
constexpr auto fmap_btree(Fn&& fn, const BinaryTreeF<int, A>& layer) {
    using B = std::remove_cvref_t<std::invoke_result_t<Fn, const A&>>;
    return BinaryTreeF<int, B>{
        layer.value,
        layer.left ? make_slot<B>(std::invoke(fn, *layer.left)) : child_slot_t<B>{},
        layer.right ? make_slot<B>(std::invoke(fn, *layer.right)) : child_slot_t<B>{},
    };
}

inline constexpr auto fmap_btree_fn = [](auto&& fn, const auto& layer) {
    return fmap_btree(std::forward<decltype(fn)>(fn), layer);
};

// In-order rendering algebra: "_" marks an absent child; the exact string
// records shape, child order, and every node value (Decision 7).
inline auto show_algebra = [](const BinaryTreeF<int, std::string>& layer) -> std::string {
    if (!layer.left && !layer.right)
        return std::to_string(layer.value);
    return "(" + (layer.left ? *layer.left : std::string{"_"}) + " " + std::to_string(layer.value) + " " +
           (layer.right ? *layer.right : std::string{"_"}) + ")";
};

// Non-commutative in-order numeric algebra: leaves yield their value;
// interior nodes yield (left - value) - right with absent children
// contributing 0. Swapped children or a flipped visit order give a
// different answer.
inline constexpr auto subtract_algebra = [](const BinaryTreeF<int, int>& layer) -> int {
    if (!layer.left && !layer.right)
        return layer.value;
    return ((layer.left ? *layer.left : 0) - layer.value) - (layer.right ? *layer.right : 0);
};

// DEV-01 non-idempotent algebra: every node contributes value - 1.
inline constexpr auto decrement_sum_algebra = [](const BinaryTreeF<int, int>& layer) -> int {
    return (layer.value - 1) + (layer.left ? *layer.left : 0) + (layer.right ? *layer.right : 0);
};

// Balanced-BST coalgebra: seed [lo, hi) becomes the node mid = midpoint,
// with child seeds [lo, mid) and [mid + 1, hi); empty ranges become absent
// children (null Box).
using Range = std::pair<int, int>;

inline constexpr auto bst_coalgebra = [](const Range& r) -> BinaryTreeF<int, Range> {
    auto [lo, hi] = r;
    int mid       = lo + (hi - lo) / 2;
    return BinaryTreeF<int, Range>{mid,
                                   mid > lo ? make_slot<Range>(Range{lo, mid}) : child_slot_t<Range>{},
                                   hi > mid + 1 ? make_slot<Range>(Range{mid + 1, hi}) : child_slot_t<Range>{}};
};

// ---------------------------------------------------------------------
// Constexpr coverage (DEV-04), Fix side: each verb through each API.
// ---------------------------------------------------------------------

static_assert(has_functor_instance<BinaryTreeF<int, int>>);
static_assert(has_functor_instance<BinaryTreeF<int, IntFixed>>);
static_assert(!has_functor_instance<IntTree>);

// ---------------------------------------------------------------------
// Allocator additivity (WP-5): BinaryTreeF grew a defaulted Allocator
// parameter (Option A, Decision 9), so every existing one/two-argument
// spelling must still name the exact same type as before.
// ---------------------------------------------------------------------

static_assert(std::is_same_v<BinaryTreeF<int, int>, BinaryTreeF<int, int, binary_tree_default_allocator>>);
static_assert(std::is_same_v<BinaryTreeF<int, IntFixed>, BinaryTreeF<int, IntFixed, binary_tree_default_allocator>>);
static_assert(std::is_same_v<child_slot_t<IntFixed>, child_slot_t<IntFixed, binary_tree_default_allocator>>);
static_assert(
    std::is_same_v<BinaryTreeFix<int>, Fix<BinaryTreeLayer<int, binary_tree_default_allocator>::template F>>);
static_assert(std::is_same_v<Box<IntFixed>, child_slot_t<IntFixed>>,
              "the knot Box must still be exactly Box<IntFixed> with the default allocator");

// fold_fix, both APIs: node(1, leaf 2, leaf 3) gives (2 - 1) - 3 == -2;
// the mirrored tree gives (3 - 1) - 2 == 0, so child order matters.
constexpr auto subtract_in_order() -> int {
    auto tree = fixed_node(1, fixed_leaf(2), fixed_leaf(3));
    return fold_fix<int>(subtract_algebra, tree);
}

constexpr auto subtract_mirrored_explicit_fmap() -> int {
    auto mirrored = fixed_node(1, fixed_leaf(3), fixed_leaf(2));
    return fold_fix<int>(subtract_algebra, fmap_btree_fn, mirrored);
}

static_assert(subtract_in_order() == -2);
static_assert(subtract_mirrored_explicit_fmap() == 0);

// unfold_fix, both APIs: BST over [0, 5) has root 2, left subtree
// (0 1 _), right subtree (3 4 _). Hand-computed subtraction: left
// (0 - 1) - 0 == -1, right (3 - 4) - 0 == -1, root (-1 - 2) - (-1) == -2.
static_assert(fold_fix<int>(subtract_algebra, unfold_fix<BinaryTreeLayer<int>::F>(bst_coalgebra, Range{0, 5})) == -2);
static_assert(fold_fix<int>(subtract_algebra,
                            fmap_btree_fn,
                            unfold_fix<BinaryTreeLayer<int>::F>(bst_coalgebra, fmap_btree_fn, Range{0, 5})) == -2);

// refold, both APIs: fused pipeline agrees with the two-pass value.
static_assert(refold<int, BinaryTreeLayer<int>::F>(subtract_algebra, bst_coalgebra, Range{0, 5}) == -2);
static_assert(refold<int, BinaryTreeLayer<int>::F>(subtract_algebra, bst_coalgebra, fmap_btree_fn, Range{0, 5}) == -2);

// DEV-01 at compile time: three nodes of 10 sum, decremented, to 27.
constexpr auto decrement_three_tens() -> int {
    auto tree = fixed_node(10, fixed_leaf(10), fixed_leaf(10));
    return fold_fix<int>(decrement_sum_algebra, tree);
}

static_assert(decrement_three_tens() == 27);

// ---------------------------------------------------------------------
// A counting allocator for the WP-5 gate: allocate()/deallocate() go
// straight through allocator_traits with no pmr indirection, so counts
// here prove the allocator-tagged to_fix/from_fix/leaf/node/make_ptr
// factories route every allocation through the SUPPLIED allocator, not a
// fixed default. Single-parameter on purpose: allocator_traits derives
// rebind_alloc<U> as tracking_alloc<U> automatically (no nested rebind
// member needed), so one type serves every rebound position (the knot
// Box, and allocate_shared's internal control-block type alike).
// ---------------------------------------------------------------------

struct alloc_stats {
    int allocations   = 0;
    int deallocations = 0;
    constexpr auto live() const -> int { return allocations - deallocations; }
};

template <typename T>
struct tracking_alloc {
    using value_type = T;

    alloc_stats* stats = nullptr;
    int          id    = 0;

    // Default-constructible (stats == nullptr) so a disengaged child_slot
    // — child_slot_t<A, Allocator>{} for an absent child — can default-
    // construct its Box's stored allocator, exactly as a default-
    // constructed std::pmr::polymorphic_allocator can; a disengaged slot
    // never calls allocate()/deallocate(), so the null stats pointer is
    // never dereferenced.
    tracking_alloc() = default;
    tracking_alloc(alloc_stats* s, int i) : stats(s), id(i) {}
    template <typename U>
    tracking_alloc(const tracking_alloc<U>& o) : stats(o.stats), id(o.id) {}

    auto allocate(std::size_t n) -> T* {
        stats->allocations += 1;
        return std::allocator<T>{}.allocate(n);
    }
    void deallocate(T* p, std::size_t n) {
        stats->deallocations += 1;
        std::allocator<T>{}.deallocate(p, n);
    }
};

} // namespace

// ---------------------------------------------------------------------
// The ported persistent tree behaves as before.
// ---------------------------------------------------------------------

TEST_CASE("BinaryTree - LeafAndNodeAccessors", "[tree_algorithms::binary_tree]") {
    auto t = IntTree::node(1, IntTree::leaf(2), IntTree::leaf(3));
    CHECK(t.value() == 1);
    REQUIRE(t.has_left());
    REQUIRE(t.has_right());
    CHECK(t.left().value() == 2);
    CHECK(t.right().value() == 3);
    CHECK_FALSE(t.left().has_left());
    CHECK_FALSE(t.right().has_right());

    auto b = IntTree::branch(1, IntTree::leaf(2), IntTree::leaf(3));
    CHECK(b.value() == t.value());
    CHECK(b.left().value() == t.left().value());
}

TEST_CASE("BinaryTree - CopiesShareStructure", "[tree_algorithms::binary_tree]") {
    // Persistence is part of the type's identity: copying the tree copies
    // shared_ptrs, so subtrees are shared, not cloned.
    auto t    = IntTree::node(1, IntTree::leaf(2), IntTree::leaf(3));
    auto copy = t;
    CHECK(copy.left_ptr().get() == t.left_ptr().get());
    CHECK(copy.right_ptr().get() == t.right_ptr().get());
}

// ---------------------------------------------------------------------
// The adapter: the same verbs run over the converted tree.
// ---------------------------------------------------------------------

TEST_CASE("BinaryTree - FoldViaTheVerbsAfterToFix", "[tree_algorithms::binary_tree]") {
    auto t = IntTree::node(1, IntTree::leaf(2), IntTree::leaf(3));
    CHECK(fold_fix<int>(subtract_algebra, to_fix(t)) == -2);
    CHECK(fold_fix<int>(subtract_algebra, fmap_btree_fn, to_fix(t)) == -2);

    // DEV-01: decremented sum over three tens.
    auto tens = IntTree::node(10, IntTree::leaf(10), IntTree::leaf(10));
    CHECK(fold_fix<int>(decrement_sum_algebra, to_fix(tens)) == 27);
}

TEST_CASE("BinaryTree - TraversalOrderIsObservable", "[tree_algorithms::binary_tree]") {
    // The rendered string pins shape and child order exactly; a fold whose
    // fmap visited right before left would produce the mirrored string.
    auto t = IntTree::node(1, IntTree::node(2, IntTree::leaf(4), IntTree::leaf(5)), IntTree::leaf(3));
    CHECK(fold_fix<std::string>(show_algebra, to_fix(t)) == "((4 2 5) 1 3)");

    auto mirrored = IntTree::node(1, IntTree::leaf(3), IntTree::node(2, IntTree::leaf(5), IntTree::leaf(4)));
    CHECK(fold_fix<std::string>(show_algebra, to_fix(mirrored)) == "(3 1 (5 2 4))");
}

TEST_CASE("BinaryTree - FromFixRoundTrip", "[tree_algorithms::binary_tree]") {
    auto t     = IntTree::node(1, IntTree::node(2, IntTree::leaf(4), IntTree::leaf(5)), IntTree::leaf(3));
    auto round = from_fix(to_fix(t));

    CHECK(round.value() == 1);
    REQUIRE(round.has_left());
    REQUIRE(round.has_right());
    CHECK(round.left().value() == 2);
    CHECK(round.right().value() == 3);
    CHECK_FALSE(round.right().has_left());

    // Same rendered structure, but rebuilt storage: no sharing with t.
    CHECK(fold_fix<std::string>(show_algebra, to_fix(round)) == fold_fix<std::string>(show_algebra, to_fix(t)));
    CHECK(round.left_ptr().get() != t.left_ptr().get());
}

TEST_CASE("BinaryTree - UnfoldBuildsABalancedTreeIntoTheAdapter", "[tree_algorithms::binary_tree]") {
    // unfold_fix builds the Fix form from a range seed; from_fix lands it
    // in the shared_ptr representation. In-order rendering shows the BST
    // shape over [0, 5): root 2, left subtree (0 1 _), right (3 4 _).
    auto fixed = unfold_fix<BinaryTreeLayer<int>::F>(bst_coalgebra, Range{0, 5});
    CHECK(fold_fix<std::string>(show_algebra, fixed) == "((0 1 _) 2 (3 4 _))");

    auto tree = from_fix(fixed);
    CHECK(tree.value() == 2);
    REQUIRE(tree.has_left());
    CHECK(tree.left().value() == 1);
    CHECK_FALSE(tree.left().has_right());
    CHECK(fold_fix<std::string>(show_algebra, to_fix(tree)) == "((0 1 _) 2 (3 4 _))");
}

TEST_CASE("BinaryTree - RefoldMatchesFoldOfUnfold", "[tree_algorithms::binary_tree]") {
    for (int hi = 1; hi <= 8; ++hi) {
        Range seed{0, hi};
        auto  materialized =
            fold_fix<std::string>(show_algebra, unfold_fix<BinaryTreeLayer<int>::F>(bst_coalgebra, seed));
        CHECK(refold<std::string, BinaryTreeLayer<int>::F>(show_algebra, bst_coalgebra, seed) == materialized);
        CHECK(refold<std::string, BinaryTreeLayer<int>::F>(show_algebra, bst_coalgebra, fmap_btree_fn, seed) ==
              materialized);
    }
}

TEST_CASE("BinaryTree - DirectFoldAgreesWithAdapterPath", "[tree_algorithms::binary_tree]") {
    // The same tree object, two routes to the same answer: convert with
    // to_fix and fold with fold_fix, or fold_with directly through a
    // projection that reads the shared_ptr children in place — no
    // conversion, no structure copied beyond one layer at a time. The
    // projection deals in raw non-owning pointers; the same BinaryTreeF
    // layer and the same fmap serve both routes.
    using Ptr    = const IntTree*;
    auto project = [](Ptr t) -> BinaryTreeF<int, Ptr> {
        return BinaryTreeF<int, Ptr>{t->value(),
                                     t->has_left() ? make_slot<Ptr>(&t->left()) : child_slot_t<Ptr>{},
                                     t->has_right() ? make_slot<Ptr>(&t->right()) : child_slot_t<Ptr>{}};
    };

    auto t = IntTree::node(1, IntTree::node(2, IntTree::leaf(4), IntTree::leaf(5)), IntTree::leaf(3));

    auto direct  = fold_with<std::string>(show_algebra, fmap_btree_fn, project, static_cast<Ptr>(&t));
    auto adapted = fold_fix<std::string>(show_algebra, to_fix(t));
    CHECK(direct == adapted);
    CHECK(direct == "((4 2 5) 1 3)");

    // Non-commutative numeric agreement on the same structure.
    CHECK(fold_with<int>(subtract_algebra, fmap_btree_fn, project, static_cast<Ptr>(&t)) ==
          fold_fix<int>(subtract_algebra, to_fix(t)));
}

TEST_CASE("BinaryTree - FmapPreservesAbsentChildrenAndValue", "[tree_algorithms::binary_tree]") {
    const auto& map = functor_typeclass<BinaryTreeF<int, int>>;

    // int is complete, so the layer's child slots are inline optionals.
    BinaryTreeF<int, int> layer{7, make_slot<int>(1), child_slot_t<int>{}};

    auto doubled = map.fmap([](const int& x) { return x * 2; }, layer);
    CHECK(doubled.value == 7);
    REQUIRE(doubled.left.has_value());
    CHECK(*doubled.left == 2);
    CHECK(!doubled.right.has_value());

    // Derived operation from the Functor CRTP base.
    auto replaced = map.replace(layer, 9);
    CHECK(replaced.value == 7);
    REQUIRE(replaced.left.has_value());
    CHECK(*replaced.left == 9);
    CHECK(!replaced.right.has_value());
}

// ---------------------------------------------------------------------
// WP-5 gate: allocator-tagged leaf/node/make_ptr build the shared_ptr
// side through a supplied allocator (D-A5); allocator-tagged
// to_fix/from_fix route the Fix side and the rebuilt shared_ptr side
// through it too (Decision 9). A tracking allocator counts every
// allocate/deallocate call directly, proving the SUPPLIED allocator was
// used, not just that the default one was left alone.
// ---------------------------------------------------------------------

TEST_CASE("BinaryTree - AllocatorTaggedLeafNodeAndMakePtr", "[tree_algorithms::binary_tree]") {
    alloc_stats             stats{};
    tracking_alloc<std::byte> a(&stats, 7);

    // A leaf allocates nothing: no children to wrap in a shared_ptr.
    auto leaf4 = IntTree::leaf(std::allocator_arg, a, 4);
    CHECK(leaf4.value() == 4);
    CHECK_FALSE(leaf4.has_left());
    CHECK_FALSE(leaf4.has_right());
    CHECK(stats.allocations == 0);

    auto leaf5 = IntTree::leaf(std::allocator_arg, a, 5);

    // node() allocates exactly one shared_ptr control block per child.
    auto inner = IntTree::node(std::allocator_arg, a, 2, std::move(leaf4), std::move(leaf5));
    CHECK(inner.value() == 2);
    CHECK(inner.left().value() == 4);
    CHECK(inner.right().value() == 5);
    CHECK(stats.allocations == 2);

    // make_ptr() allocates one more, wrapping the whole subtree.
    auto ptr = IntTree::make_ptr(std::allocator_arg, a, std::move(inner));
    CHECK(stats.allocations == 3);
    CHECK(ptr->value() == 2);
    CHECK(ptr->left().value() == 4);
}

TEST_CASE("BinaryTree - AllocatorTaggedToFixFromFixRouteThroughTheSuppliedAllocator", "[tree_algorithms::binary_tree]") {
    // node(1, node(2, leaf(4), leaf(5)), leaf(3)): 4 non-root nodes, so 4
    // engaged child positions overall.
    auto t = IntTree::node(1, IntTree::node(2, IntTree::leaf(4), IntTree::leaf(5)), IntTree::leaf(3));

    alloc_stats               to_fix_stats{};
    tracking_alloc<std::byte> to_fix_alloc(&to_fix_stats, 1);
    alloc_stats               from_fix_stats{};
    tracking_alloc<std::byte> from_fix_alloc(&from_fix_stats, 2);

    {
        auto fixed = to_fix(std::allocator_arg, to_fix_alloc, t);

        // 4 engaged child positions -> 4 knot Box allocations, all live
        // while `fixed` is.
        CHECK(to_fix_stats.allocations == 4);
        CHECK(to_fix_stats.live() == 4);

        // Every knot Box on the built tree carries our allocator (its id,
        // not just "some allocator of the right type").
        const auto& root = unwrap_fix(fixed);
        REQUIRE(root.left);
        REQUIRE(root.right);
        CHECK(root.left.get_allocator().id == to_fix_alloc.id);
        CHECK(root.right.get_allocator().id == to_fix_alloc.id);
        const auto& left = unwrap_fix(*root.left);
        REQUIRE(left.left);
        REQUIRE(left.right);
        CHECK(left.left.get_allocator().id == to_fix_alloc.id);
        CHECK(left.right.get_allocator().id == to_fix_alloc.id);

        // Round-trip through the shared_ptr side: from_fix's rebuild
        // algebra threads a SEPARATE allocator convention into
        // allocate_shared for every reconstructed child, proving the two
        // sides (Fix-side Box, shared_ptr-side control block) each honor
        // their own tag independently.
        auto round = from_fix(std::allocator_arg, from_fix_alloc, fixed);

        CHECK(from_fix_stats.allocations == 4);
        CHECK(round.value() == 1);
        CHECK(round.left().value() == 2);
        CHECK(round.right().value() == 3);
        CHECK(round.left().left().value() == 4);
        CHECK(round.left().right().value() == 5);
        CHECK(fold_fix<std::string>(show_algebra, to_fix(round)) == fold_fix<std::string>(show_algebra, to_fix(t)));
    }
    // `fixed` (and every knot Box it owned) is destroyed at the end of the
    // block above: allocation/deallocation balance on to_fix_alloc is the
    // final proof that the tagged conversion neither leaked nor freed
    // through some other allocator.
    CHECK(to_fix_stats.deallocations == 4);
    CHECK(to_fix_stats.live() == 0);
}

TEST_CASE("BinaryTree - PmrToFixFromFixRouteThroughTheResource", "[tree_algorithms::binary_tree]") {
    // Option A propagation proof, mirrored on the WP-4/WP-5-expr pmr
    // gate tests: a monotonic pool over a null_memory_resource upstream
    // — any allocation escaping the buffer would throw. to_fix's knot
    // Boxes are directly queryable (get_allocator().resource()); from_fix's
    // shared_ptr side is not (allocate_shared's control block is opaque,
    // per D-A5's "no per-node storage" trade), so a completed round trip
    // with no throw is the proof there.
    using PA = std::pmr::polymorphic_allocator<std::byte>;

    alignas(std::max_align_t) unsigned char             buffer[16 * 1024];
    std::pmr::monotonic_buffer_resource pool(buffer, sizeof(buffer), std::pmr::null_memory_resource());
    PA                                   a(&pool);

    auto t = IntTree::node(1, IntTree::node(2, IntTree::leaf(4), IntTree::leaf(5)), IntTree::leaf(3));

    auto fixed = to_fix(std::allocator_arg, a, t);

    const auto& root = unwrap_fix(fixed);
    REQUIRE(root.left);
    REQUIRE(root.right);
    CHECK(root.left.get_allocator().resource() == &pool);
    CHECK(root.right.get_allocator().resource() == &pool);
    const auto& left = unwrap_fix(*root.left);
    CHECK(left.left.get_allocator().resource() == &pool);
    CHECK(left.right.get_allocator().resource() == &pool);

    auto round = from_fix(std::allocator_arg, a, fixed);
    CHECK(round.value() == 1);
    CHECK(round.left().value() == 2);
    CHECK(round.right().value() == 3);
    CHECK(round.left().left().value() == 4);
    CHECK(round.left().right().value() == 5);

    CHECK(fold_fix<std::string>(show_algebra, to_fix(round)) == fold_fix<std::string>(show_algebra, to_fix(t)));
}
