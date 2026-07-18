// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/tree_algorithms/pmr.hpp>
#include <beman/tree_algorithms/pmr.hpp> // Re-inclusion: verifies include guard

#include <beman/tree_algorithms/binary_tree.hpp>
#include <beman/tree_algorithms/box.hpp>
#include <beman/tree_algorithms/expression.hpp>
#include <beman/tree_algorithms/fold_map_lookup.hpp>
#include <beman/tree_algorithms/fringe_tree.hpp>
#include <beman/tree_algorithms/overloaded.hpp>
#include <beman/tree_algorithms/recursion_schemes.hpp>
#include <beman/tree_algorithms/rose_tree.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <memory>
#include <memory_resource>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

using beman::tree_algorithms::Add;
using beman::tree_algorithms::BinaryTree;
using beman::tree_algorithms::BinaryTreeF;
using beman::tree_algorithms::BinaryTreeLayer;
using beman::tree_algorithms::Box;
using beman::tree_algorithms::Const;
using beman::tree_algorithms::ExprLayer;
using beman::tree_algorithms::Fix;
using beman::tree_algorithms::fold_fix;
using beman::tree_algorithms::fold_map;
using beman::tree_algorithms::fold_with;
using beman::tree_algorithms::fringe_tree_project;
using beman::tree_algorithms::FringeBranch;
using beman::tree_algorithms::FringeEmpty;
using beman::tree_algorithms::FringeLeaf;
using beman::tree_algorithms::FringeTree;
using beman::tree_algorithms::FringeTreeF;
using beman::tree_algorithms::functor_typeclass;
using beman::tree_algorithms::has_functor_instance;
using beman::tree_algorithms::has_layer_fold_instance;
using beman::tree_algorithms::make_slot;
using beman::tree_algorithms::Mul;
using beman::tree_algorithms::overloaded;
using beman::tree_algorithms::rose_children_t;
using beman::tree_algorithms::RoseF;
using beman::tree_algorithms::RoseLayer;
using beman::tree_algorithms::unfold_fix;
using beman::tree_algorithms::unfold_with;
using beman::tree_algorithms::unwrap_fix;

namespace pmr = beman::tree_algorithms::pmr;

namespace {

// ---------------------------------------------------------------------
// A counting memory_resource, reused from the WP-6 pattern (DEV-A05):
// std::shared_ptr exposes no get_allocator(), so the BinaryTree and
// FringeTree gates below prove routing by an exact forwarded
// allocate/deallocate count rather than a get_allocator() read-back.
// ---------------------------------------------------------------------

class CountingResource : public std::pmr::memory_resource {
    std::pmr::memory_resource* d_upstream;

  public:
    int allocate_count   = 0;
    int deallocate_count = 0;

    explicit CountingResource(std::pmr::memory_resource* upstream) : d_upstream(upstream) {}

  private:
    auto do_allocate(std::size_t bytes, std::size_t alignment) -> void* override {
        ++allocate_count;
        return d_upstream->allocate(bytes, alignment);
    }

    void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override {
        ++deallocate_count;
        d_upstream->deallocate(p, bytes, alignment);
    }

    auto do_is_equal(const std::pmr::memory_resource& other) const noexcept -> bool override { return this == &other; }
};

} // namespace

// ---------------------------------------------------------------------
// Constexpr coverage (DEV-04): this header is deliberately runtime-only
// (std::pmr::polymorphic_allocator dispatches through a virtual
// std::pmr::memory_resource*, so it is not a constexpr-usable type), so
// there is no constexpr round trip to pin the way sibling headers do.
// What IS pinnable at compile time, and pinned here, is the type-level
// contract: every pmr alias names exactly the type its owning
// representation's own header already promised (the HANDOFF-WP4/5/5-
// expr/6 "Interface decided" sections), and the generalized
// functor_typeclass/layer_fold_typeclass registrations from those WPs
// reach the pmr-allocator instantiation with no further registration
// needed here (WP-4's "falls out" claim, confirmed for every
// representation that has a layer-fold instance).
// ---------------------------------------------------------------------

static_assert(std::is_same_v<pmr::RoseTreeFix<int>, Fix<RoseLayer<int, pmr::allocator_type>::template F> >);
static_assert(std::is_same_v<pmr::BinaryTreeFix<int>, Fix<BinaryTreeLayer<int, pmr::allocator_type>::template F> >);
static_assert(std::is_same_v<pmr::Expr, Fix<ExprLayer<pmr::allocator_type>::template F> >);
static_assert(std::is_same_v<pmr::Box<int>, Box<int, std::pmr::polymorphic_allocator<int> > >);

static_assert(has_functor_instance<RoseF<int, int, pmr::allocator_type> >);
static_assert(has_functor_instance<BinaryTreeF<int, int, pmr::allocator_type> >);
static_assert(has_functor_instance<ExprLayer<pmr::allocator_type>::template F<int> >);
static_assert(has_functor_instance<FringeTreeF<int, int> >);

static_assert(has_layer_fold_instance<RoseF<int, int, pmr::allocator_type> >);
static_assert(has_layer_fold_instance<BinaryTreeF<int, int, pmr::allocator_type> >);
static_assert(has_layer_fold_instance<ExprLayer<pmr::allocator_type>::template F<int> >);
static_assert(has_layer_fold_instance<FringeTreeF<int, int> >);

// ---------------------------------------------------------------------
// WP-7 gate: for every representation, build (and where the
// representation supports it, unfold) a tree against a
// std::pmr::monotonic_buffer_resource whose upstream is
// std::pmr::null_memory_resource() -- so any allocation that escaped the
// buffer would throw, and a completed build/fold/unfold is itself proof
// that nothing leaked to the default resource. Each fold below uses a
// non-commutative, shape-and-order-pinning combine (DEV-01), and each
// test asserts polymorphic_allocator propagation directly where the
// representation exposes get_allocator() (rose, expression, the
// BinaryTree Fix side), or via the CountingResource decorator's exact
// counts where it does not (BinaryTree's and FringeTree's shared_ptr
// spines, DEV-A05).
// ---------------------------------------------------------------------

TEST_CASE("Pmr - RoseTreeBuildFoldAndUnfoldAgainstNullUpstream", "[tree_algorithms::pmr]") {
    alignas(std::max_align_t) unsigned char buffer[16 * 1024];
    std::pmr::monotonic_buffer_resource     pool(buffer, sizeof(buffer), std::pmr::null_memory_resource());
    pmr::allocator_type                     a(&pool);
    CHECK(a.resource() == &pool);

    using PmrTree  = pmr::RoseTreeFix<int>;
    using ChildVec = rose_children_t<PmrTree, pmr::allocator_type>;

    // Build bottom-up via pmr::rose(); already-built subtrees are MOVED
    // into the children vector, never brace-init-copied (WP-4's
    // documented foot-gun: PmrTree is not itself allocator-aware, so a
    // copy's select_on_container_copy_construction would silently reset
    // the copy's nested children off the pool).
    auto     n4 = pmr::rose(a, 4);
    ChildVec n3_children;
    n3_children.push_back(std::move(n4));
    auto n3 = pmr::rose(a, 3, std::move(n3_children));

    auto     n2 = pmr::rose(a, 2);
    auto     n5 = pmr::rose(a, 5);
    ChildVec top_children;
    top_children.push_back(std::move(n2));
    top_children.push_back(std::move(n3));
    top_children.push_back(std::move(n5));
    auto t = pmr::rose(a, 1, std::move(top_children));

    const auto& root = unwrap_fix(t);
    CHECK(root.children.get_allocator().resource() == &pool);
    CHECK(root.children.get_allocator() == a);
    const auto& node3 = unwrap_fix(root.children[1]);
    CHECK(node3.value == 3);
    CHECK(node3.children.get_allocator().resource() == &pool);

    // Non-commutative string concatenation via the lookup tier pins both
    // shape and order, and proves the generalized functor_typeclass/
    // layer_fold_typeclass registrations reach this pmr layer with no
    // further registration (WP-4).
    auto show   = [](int x) { return std::to_string(x); };
    auto concat = [](const std::string& x, const std::string& y) { return x + y; };
    CHECK(fold_map<std::string>(show, concat, std::string{}, t) == "12345");

    // Unfold an entire tree from a fresh seed via unfold_fix +
    // pmr::rose_fmap(a): the coalgebra's own children vector has no real
    // resource to propagate, so only the captured allocator in
    // rose_fmap routes the build (WP-4's fold/unfold asymmetry).
    using Seed     = std::pair<int, int>;
    auto coalgebra = [](const Seed& s) -> RoseF<int, Seed, pmr::allocator_type> {
        auto [label, fanout] = s;
        rose_children_t<Seed, pmr::allocator_type> children;
        for (int i = 1; i <= fanout; ++i) {
            children.push_back(Seed{label * 10 + i, fanout - 1});
        }
        return RoseF<int, Seed, pmr::allocator_type>{label, std::move(children)};
    };
    auto unfolded =
        unfold_fix<RoseLayer<int, pmr::allocator_type>::template F>(coalgebra, pmr::rose_fmap(a), Seed{1, 2});

    const auto& uroot = unwrap_fix(unfolded);
    CHECK(uroot.children.get_allocator().resource() == &pool);
    CHECK(uroot.children.get_allocator() == a);

    auto combine = [](int x, int y) { return x * 10 + y; };
    CHECK(fold_map<int>([](int x) { return x; }, combine, 0, unfolded) == 2551);
}

TEST_CASE("Pmr - ExpressionTreeBuildFoldAndUnfoldAgainstNullUpstream", "[tree_algorithms::pmr]") {
    alignas(std::max_align_t) unsigned char buffer[16 * 1024];
    std::pmr::monotonic_buffer_resource     pool(buffer, sizeof(buffer), std::pmr::null_memory_resource());
    pmr::allocator_type                     a(&pool);

    using PmrExpr = pmr::Expr;

    // Build via the pmr smart constructors directly -- expression.hpp
    // offers no allocator-tagged add_node/mul_node of its own (its
    // allocator-aware build path is expr_fmap + unfold_fix, per
    // WP-5-expr), so these are pmr.hpp's own, built on the same tagged
    // make_slot/wrap_fix every representation's tagged factories use:
    // (1 + 2) * 3.
    auto tree =
        pmr::mul_node(a, pmr::add_node(a, pmr::const_node(a, 1), pmr::const_node(a, 2)), pmr::const_node(a, 3));

    const auto& root = unwrap_fix(tree);
    REQUIRE(std::holds_alternative<Mul<PmrExpr, pmr::allocator_type> >(root));
    const auto& mul = std::get<Mul<PmrExpr, pmr::allocator_type> >(root);
    CHECK(mul.left.get_allocator().resource() == &pool);
    CHECK(mul.left.get_allocator() == a);
    CHECK(mul.right.get_allocator().resource() == &pool);
    const auto& mul_left_layer = unwrap_fix(*mul.left);
    REQUIRE(std::holds_alternative<Add<PmrExpr, pmr::allocator_type> >(mul_left_layer));
    const auto& add = std::get<Add<PmrExpr, pmr::allocator_type> >(mul_left_layer);
    CHECK(add.left.get_allocator().resource() == &pool);

    // Non-commutative, shape-pinning print algebra folded via the
    // lookup tier -- proves fold over a pmr expression layer works out
    // of the box (WP-5-expr's "lookup fold works, lookup unfold does
    // not" contrast).
    auto print_pmr = [](const ExprLayer<pmr::allocator_type>::template F<std::string>& layer) -> std::string {
        return std::visit(
            overloaded{
                [](const Const<std::string>& c) { return std::to_string(c.val); },
                [](const Add<std::string, pmr::allocator_type>& x) { return "(" + *x.left + " + " + *x.right + ")"; },
                [](const Mul<std::string, pmr::allocator_type>& x) { return "(" + *x.left + " * " + *x.right + ")"; },
            },
            layer);
    };
    CHECK(fold_fix<std::string>(print_pmr, tree) == "((1 + 2) * 3)");

    // Unfold an entire tree onto the pool via unfold_fix +
    // pmr::expr_fmap(a); the null-upstream buffer means any escape to
    // new_delete_resource would throw, so completing the build is
    // itself proof of zero leakage.
    using Range = std::pair<int, int>;
    auto split  = [](const Range& r) -> ExprLayer<pmr::allocator_type>::template F<Range> {
        auto [lo, hi] = r;
        if (hi - lo <= 1)
            return Const<Range>{lo};
        int mid = lo + (hi - lo) / 2;
        return Add<Range, pmr::allocator_type>{make_slot<Range>(Range{lo, mid}), make_slot<Range>(Range{mid, hi})};
    };
    auto unfolded = unfold_fix<ExprLayer<pmr::allocator_type>::template F>(split, pmr::expr_fmap(a), Range{0, 4});

    const auto& uroot = unwrap_fix(unfolded);
    REQUIRE(std::holds_alternative<Add<PmrExpr, pmr::allocator_type> >(uroot));
    const auto& uadd = std::get<Add<PmrExpr, pmr::allocator_type> >(uroot);
    CHECK(uadd.left.get_allocator().resource() == &pool);
    CHECK(uadd.left.get_allocator() == a);

    CHECK(fold_fix<std::string>(print_pmr, unfolded) == "((0 + 1) + (2 + 3))");
}

TEST_CASE("Pmr - BinaryTreeToFixFoldFromFixAgainstNullUpstream", "[tree_algorithms::pmr]") {
    alignas(std::max_align_t) unsigned char buffer[16 * 1024];
    std::pmr::monotonic_buffer_resource     pool(buffer, sizeof(buffer), std::pmr::null_memory_resource());
    CountingResource                        counted(&pool);
    pmr::allocator_type                     a(&counted);
    CHECK(a.resource() == &counted);

    {
        // node(1, node(2, leaf 4, leaf 5), leaf 3): 4 non-root nodes, so
        // 4 engaged child positions -- pmr::node() allocate_shared's one
        // control block per engaged child, so 4 allocations here.
        auto t = pmr::node(a, 1, pmr::node(a, 2, pmr::leaf(a, 4), pmr::leaf(a, 5)), pmr::leaf(a, 3));
        CHECK(counted.allocate_count == 4);

        // to_fix: one knot Box per engaged child position, the same 4
        // positions -> 4 more allocations. Box uses raw allocate/
        // deallocate rather than allocate_shared, but both route through
        // the same polymorphic_allocator, so both land on `counted`.
        auto fixed = pmr::to_fix(a, t);
        CHECK(counted.allocate_count == 8);

        const auto& root = unwrap_fix(fixed);
        REQUIRE(root.left);
        REQUIRE(root.right);
        CHECK(root.left.get_allocator().resource() == &counted);
        CHECK(root.left.get_allocator() == a);
        const auto& left = unwrap_fix(*root.left);
        REQUIRE(left.left);
        CHECK(left.left.get_allocator().resource() == &counted);

        // Non-commutative, shape-and-order-pinning in-order fold via the
        // lookup tier -- lookup fold over a pmr BinaryTreeF layer works
        // out of the box provided the algebra names the layer generically
        // (WP-5's own warning: an algebra hardcoded to the 2-argument
        // BinaryTreeF<T, Result> alias will not accept this layer).
        auto show_pmr = [](const BinaryTreeF<int, std::string, pmr::allocator_type>& layer) -> std::string {
            if (!layer.left && !layer.right)
                return std::to_string(layer.value);
            return "(" + (layer.left ? *layer.left : std::string{"_"}) + " " + std::to_string(layer.value) + " " +
                   (layer.right ? *layer.right : std::string{"_"}) + ")";
        };
        CHECK(fold_fix<std::string>(show_pmr, fixed) == "((4 2 5) 1 3)");

        // from_fix: 4 more allocate_shared calls rebuilding the spine;
        // the rebuilt shared_ptr side is not independently queryable
        // (DEV-A05: no get_allocator() on shared_ptr), so the exact
        // count -- not a resource() read-back -- is the proof here.
        auto round = pmr::from_fix(a, fixed);
        CHECK(counted.allocate_count == 12);
        CHECK(round.value() == 1);
        CHECK(round.left().value() == 2);
        CHECK(round.left().left().value() == 4);
        CHECK(round.left().right().value() == 5);
        CHECK(round.right().value() == 3);
    }
    // Every allocation above -- the shared_ptr spine, the Fix-side knot
    // Boxes, and the rebuilt spine -- is deallocated once its owner goes
    // out of scope: no leak, and the null-upstream pool means none of it
    // could have escaped to new_delete_resource without throwing during
    // the build.
    CHECK(counted.deallocate_count == counted.allocate_count);
}

TEST_CASE("Pmr - FringeTreeBranchAndUnfoldAgainstNullUpstream", "[tree_algorithms::pmr]") {
    alignas(std::max_align_t) unsigned char buffer[16 * 1024];
    std::pmr::monotonic_buffer_resource     pool(buffer, sizeof(buffer), std::pmr::null_memory_resource());
    CountingResource                        counted(&pool);
    pmr::allocator_type                     a(&counted);
    CHECK(a.resource() == &counted);

    const auto& fmap_fn = [](auto&& fn, const auto& layer) {
        using Layer = std::remove_cvref_t<decltype(layer)>;
        return functor_typeclass<Layer>.fmap(std::forward<decltype(fn)>(fn), layer);
    };

    {
        // ((1 2) (3 4)): three branch() calls, each of which makes
        // exactly two allocate_shared calls (one per child), all routed
        // through the tagged branch overload -- 6 total.
        auto t = pmr::branch(a,
                             pmr::branch(a, FringeTree<int>::leaf(1), FringeTree<int>::leaf(2)),
                             pmr::branch(a, FringeTree<int>::leaf(3), FringeTree<int>::leaf(4)));
        CHECK(counted.allocate_count == 6);
        CHECK(t.flatten() == std::vector<int>{1, 2, 3, 4});
        CHECK(t.measure() == 4U);

        // Non-commutative, shape-pinning fold via the direct verbs: this
        // representation has no Fix form (WP-6), so fold_with over the
        // tree's own projection is the only fold path.
        auto shape = [](const FringeTreeF<int, std::string>& layer) -> std::string {
            return std::visit(
                overloaded{
                    [](const FringeEmpty&) -> std::string { return "()"; },
                    [](const FringeLeaf<int>& l) { return std::to_string(l.value); },
                    [](const FringeBranch<std::string>& b) { return "(" + b.left + " " + b.right + ")"; },
                },
                layer);
        };
        auto show =
            fold_with<std::string>(shape, fmap_fn, fringe_tree_project, static_cast<const FringeTree<int>*>(&t));
        CHECK(show == "((1 2) (3 4))");

        // Unfold an entire tree onto the pool via unfold_with +
        // pmr::fringe_tree_embed_alloc(a): [0, 4) splits into a full
        // binary tree of 4 leaves and 3 branch nodes, so 6 more
        // allocations.
        using Range = std::pair<int, int>;
        auto split  = [](const Range& r) -> FringeTreeF<int, Range> {
            auto [lo, hi] = r;
            if (hi - lo <= 0)
                return FringeEmpty{};
            if (hi - lo == 1)
                return FringeLeaf<int>{lo};
            int mid = lo + (hi - lo) / 2;
            return FringeBranch<Range>{Range{lo, mid}, Range{mid, hi}};
        };
        auto tree2 = unfold_with<FringeTree<int> >(split, fmap_fn, pmr::fringe_tree_embed_alloc(a), Range{0, 4});
        CHECK(counted.allocate_count == 12);
        CHECK(tree2.flatten() == std::vector<int>{0, 1, 2, 3});
        auto show2 =
            fold_with<std::string>(shape, fmap_fn, fringe_tree_project, static_cast<const FringeTree<int>*>(&tree2));
        CHECK(show2 == "((0 1) (2 3))");
    }
    // Balanced teardown: every allocate_shared call above is matched by
    // a deallocate once its owner is destroyed, and (because the pool
    // sits on a null_memory_resource upstream) no allocation could have
    // escaped to the default resource during either build without
    // throwing.
    CHECK(counted.deallocate_count == counted.allocate_count);
}
