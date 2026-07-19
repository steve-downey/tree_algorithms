// benchmarks/beman/tree_algorithms/allocator.bench.cpp             -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Runtime cost of the allocator that backs a Fix tree's knot Boxes: the
// campaign's thesis (docs/notes/allocator-awareness-plan.md, WP-8) is that
// per-node new/delete at the knot is the dominant cost of building and
// tearing down a tree, so a monotonic or pool resource should widen the
// Fix advantage over the naive hand-rolled baselines.
//
// Three questions, three sections, one representation (BinaryTree /
// BinaryTreeF, the same one build.bench.cpp uses, so these numbers are
// directly comparable to that file's):
//
//   build     Convert an already-built native BinaryTree (shared_ptr
//             spine, built once, outside the timed region) into its Fix
//             form under each of three allocators: default std::allocator
//             (via to_fix), a fresh std::pmr::monotonic_buffer_resource
//             (via pmr::to_fix), and a fresh
//             std::pmr::unsynchronized_pool_resource (via pmr::to_fix).
//             The monotonic/pool rows construct their resource inside the
//             timed callable, once per sample, because standing the
//             resource up is itself part of what a caller pays to use it
//             — the plan asks for this explicitly. Naive unique_ptr and
//             shared_ptr (and std::indirect where available) builds over
//             the same range-split shape are the baselines.
//
//   fold      Fold each of the three Fix trees (built once, outside the
//             timed region, resources kept alive alongside) to the same
//             checkable sum via fold_map. The claim under test is that
//             reads are resource-independent — a fold never allocates, so
//             the three rows should sit on top of one another regardless
//             of what backs the tree they walk. A native shared_ptr fold
//             is included as a reference point, matching fold.bench.cpp.
//
//   teardown  The headline: build a tree once (untimed setup) and time
//             only its destructor, isolated via Catch2's
//             BENCHMARK_ADVANCED + Chronometer::measure idiom (the plain
//             BENCHMARK macro bundles a sample's construction *and*
//             destruction together — see the note below — so isolating
//             teardown needs the advanced form). Default std::allocator
//             frees every node individually; monotonic_buffer_resource
//             frees nothing per node (the whole arena drops at once, not
//             timed here — only the tree's own destructor is); pool
//             resource returns nodes to free lists. Naive shared_ptr and
//             unique_ptr teardowns (both plain new/delete) are the
//             baselines.
//
// A note on what Catch2's plain BENCHMARK macro actually measures: the
// callable's return value is a temporary materialized and destroyed
// within one full expression inside the timed loop (see
// catch_optimizer.hpp's invoke_deoptimized), so every "build" row above —
// in this file and in build.bench.cpp — measures construct-then-destroy
// per sample, not construct alone. That is consistent across every build
// row here (including the naive baselines), so the *comparison* between
// rows is still honest; it is the teardown section, using the advanced
// form, that isolates destruction on its own.
//
// The build/fold sections reuse build.bench.cpp's kBuildNodes
// (2^16 - 1, a perfectly balanced complete tree with no single-child
// nodes) so the numbers sit next to that file's. The teardown section
// uses a smaller kTeardownNodes (2^12 - 1): Catch2's advanced-benchmark
// idiom repeats the *untimed* per-sample setup (building meter.runs()
// full trees) across every calibration doubling and every sample, so at
// the full build size a teardown benchmark would spend far more wall
// time standing trees up than the destructor calls it actually measures.

#include <beman/tree_algorithms/binary_tree.hpp>
#include <beman/tree_algorithms/pmr.hpp>

#include "naive_trees.hpp"

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/benchmark/catch_chronometer.hpp>
#include <catch2/benchmark/catch_constructor.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <utility>
#include <vector>

using beman::tree_algorithms::BinaryTree;
using beman::tree_algorithms::BinaryTreeFix;
using beman::tree_algorithms::fold_map;
using beman::tree_algorithms::to_fix;

namespace pmr = beman::tree_algorithms::pmr;

namespace {

// Nodes in the build/fold trees: build.bench.cpp's own kBuildNodes, so
// these rows are directly comparable to that file's.
constexpr std::size_t kBuildNodes = (std::size_t{1} << 16) - 1;

// Nodes in the teardown trees: smaller, for the reason given in the file
// prolog (the advanced-benchmark idiom's untimed setup is repeated many
// times over, once per calibration doubling and once per sample).
constexpr std::size_t kTeardownNodes = (std::size_t{1} << 12) - 1;

using Fixed    = BinaryTreeFix<std::int64_t>;
using PmrFixed = pmr::BinaryTreeFix<std::int64_t>;
using Range    = std::pair<std::size_t, std::size_t>;

constexpr auto add = [](std::int64_t a, std::int64_t b) -> std::int64_t { return a + b; };
constexpr auto id  = [](std::int64_t v) -> std::int64_t { return v; };

// The closed-form sum of a tree whose N nodes are numbered 0 .. N-1 by the
// midpoint-split coalgebra below — the same closed form build.bench.cpp
// checks its own range-split trees against.
constexpr auto expected_sum(std::size_t n) -> std::int64_t {
    return static_cast<std::int64_t>(n) * (static_cast<std::int64_t>(n) - 1) / 2;
}

// Build a native (shared_ptr, default new/delete) BinaryTree by splitting
// [lo, hi) at its midpoint — the shape build.bench.cpp's coalgebra grows.
// This is both the tree every allocator build below converts *from* (via
// to_fix / pmr::to_fix) and, timed on its own, the naive shared_ptr
// baseline. from_children_ptrs (not node()) is used so a range that
// splits asymmetrically — not the case for kBuildNodes or kTeardownNodes,
// both 2^k - 1, but true in general — still builds correctly.
auto build_native_range(const Range& r) -> BinaryTree<std::int64_t> {
    auto [lo, hi]                                  = r;
    std::size_t                                mid = lo + (hi - lo) / 2;
    std::shared_ptr<BinaryTree<std::int64_t> > left =
        mid > lo ? std::make_shared<BinaryTree<std::int64_t> >(build_native_range(Range{lo, mid})) : nullptr;
    std::shared_ptr<BinaryTree<std::int64_t> > right =
        hi > mid + 1 ? std::make_shared<BinaryTree<std::int64_t> >(build_native_range(Range{mid + 1, hi})) : nullptr;
    return BinaryTree<std::int64_t>::from_children_ptrs(
        static_cast<std::int64_t>(mid), std::move(left), std::move(right));
}

// The hand-written unique_ptr baseline, over the same range split
// (build.bench.cpp's build_unique, reproduced here so this file stands on
// its own).
auto build_unique(const Range& r) -> std::unique_ptr<bench::UniqueTree> {
    auto [lo, hi]    = r;
    std::size_t mid  = lo + (hi - lo) / 2;
    auto        node = std::make_unique<bench::UniqueTree>();
    node->value      = static_cast<std::int64_t>(mid);
    if (mid > lo) {
        node->left = build_unique(Range{lo, mid});
    }
    if (hi > mid + 1) {
        node->right = build_unique(Range{mid + 1, hi});
    }
    return node;
}

#ifdef __cpp_lib_indirect
auto build_indirect(const Range& r) -> bench::IndirectTree {
    auto [lo, hi]           = r;
    std::size_t         mid = lo + (hi - lo) / 2;
    bench::IndirectTree node;
    node.value = static_cast<std::int64_t>(mid);
    if (mid > lo) {
        node.left.emplace(build_indirect(Range{lo, mid}));
    }
    if (hi > mid + 1) {
        node.right.emplace(build_indirect(Range{mid + 1, hi}));
    }
    return node;
}
#endif // __cpp_lib_indirect

// Bundles a per-sample pmr resource with the Fix tree built on it: the
// tree's knot Boxes hold a pmr::polymorphic_allocator that is really a
// raw pointer into *resource, so resource must outlive tree's destructor.
// Declaration order controls destruction order (members are destroyed in
// reverse of declaration): resource is declared first, so it is destroyed
// last, after tree has already deallocated through it.
struct BuiltMonoTree {
    std::unique_ptr<std::pmr::monotonic_buffer_resource> resource;
    PmrFixed                                             tree;
};

struct BuiltPoolTree {
    std::unique_ptr<std::pmr::unsynchronized_pool_resource> resource;
    PmrFixed                                                tree;
};

// Fresh std::pmr::monotonic_buffer_resource per call, growing from its
// default upstream (new_delete_resource()): materializes the Fix spine of
// an already-built native tree onto it via pmr::to_fix.
auto build_mono(const BinaryTree<std::int64_t>& native) -> BuiltMonoTree {
    auto                resource = std::make_unique<std::pmr::monotonic_buffer_resource>();
    pmr::allocator_type a(resource.get());
    PmrFixed            tree = pmr::to_fix(a, native);
    return BuiltMonoTree{std::move(resource), std::move(tree)};
}

// Fresh std::pmr::unsynchronized_pool_resource per call, same shape as
// build_mono above.
auto build_pool(const BinaryTree<std::int64_t>& native) -> BuiltPoolTree {
    auto                resource = std::make_unique<std::pmr::unsynchronized_pool_resource>();
    pmr::allocator_type a(resource.get());
    PmrFixed            tree = pmr::to_fix(a, native);
    return BuiltPoolTree{std::move(resource), std::move(tree)};
}

} // namespace

TEST_CASE("allocator: header wired up", "[allocator][bootstrap]") { REQUIRE(true); }

TEST_CASE("allocator: build a large tree under default vs monotonic vs pool", "[allocator][build][!benchmark]") {
    const Range        whole{0, kBuildNodes};
    const std::int64_t expected = expected_sum(kBuildNodes);

    const auto native = build_native_range(whole);

    // Law first: every build route, whatever allocator backs it, produces
    // a tree that folds to the same checkable sum.
    REQUIRE(fold_map<std::int64_t>(id, add, std::int64_t{0}, to_fix(native)) == expected);
    REQUIRE(fold_map<std::int64_t>(id, add, std::int64_t{0}, build_mono(native).tree) == expected);
    REQUIRE(fold_map<std::int64_t>(id, add, std::int64_t{0}, build_pool(native).tree) == expected);
    REQUIRE(bench::sum(*build_unique(whole)) == expected);
    REQUIRE(fold_map<std::int64_t>(id, add, std::int64_t{0}, build_native_range(whole)) == expected);
#ifdef __cpp_lib_indirect
    REQUIRE(bench::sum(build_indirect(whole)) == expected);
#endif

    BENCHMARK("to_fix (std::allocator, default)") { return to_fix(native); };
    BENCHMARK("pmr::to_fix (monotonic_buffer_resource, fresh per sample)") { return build_mono(native); };
    BENCHMARK("pmr::to_fix (unsynchronized_pool_resource, fresh per sample)") { return build_pool(native); };
    BENCHMARK("hand build (shared_ptr, new/delete baseline)") { return build_native_range(whole); };
    BENCHMARK("hand build (unique_ptr, new/delete baseline)") { return build_unique(whole); };
#ifdef __cpp_lib_indirect
    BENCHMARK("hand build (std::indirect, new/delete baseline)") { return build_indirect(whole); };
#endif
}

TEST_CASE("allocator: fold reads are resource-independent", "[allocator][fold][!benchmark]") {
    const Range        whole{0, kBuildNodes};
    const std::int64_t expected = expected_sum(kBuildNodes);

    const auto native = build_native_range(whole);

    // Built once, outside the timed region, one Fix tree per allocator;
    // the pmr resources are declared before their trees so they outlive
    // them (reverse-declaration-order destruction).
    const Fixed default_tree = to_fix(native);

    std::pmr::monotonic_buffer_resource mono_resource;
    pmr::allocator_type                 mono_alloc(&mono_resource);
    const PmrFixed                      mono_tree = pmr::to_fix(mono_alloc, native);

    std::pmr::unsynchronized_pool_resource pool_resource;
    pmr::allocator_type                    pool_alloc(&pool_resource);
    const PmrFixed                         pool_tree = pmr::to_fix(pool_alloc, native);

    // Law first: the same fold_map call, over four representations of the
    // same tree, produces the same checkable sum.
    REQUIRE(fold_map<std::int64_t>(id, add, std::int64_t{0}, native) == expected);
    REQUIRE(fold_map<std::int64_t>(id, add, std::int64_t{0}, default_tree) == expected);
    REQUIRE(fold_map<std::int64_t>(id, add, std::int64_t{0}, mono_tree) == expected);
    REQUIRE(fold_map<std::int64_t>(id, add, std::int64_t{0}, pool_tree) == expected);

    BENCHMARK("fold_map / shared_ptr native (baseline)") {
        return fold_map<std::int64_t>(id, add, std::int64_t{0}, native);
    };
    BENCHMARK("fold_map / Fix<Box> (std::allocator)") {
        return fold_map<std::int64_t>(id, add, std::int64_t{0}, default_tree);
    };
    BENCHMARK("fold_map / Fix<Box> (monotonic_buffer_resource)") {
        return fold_map<std::int64_t>(id, add, std::int64_t{0}, mono_tree);
    };
    BENCHMARK("fold_map / Fix<Box> (unsynchronized_pool_resource)") {
        return fold_map<std::int64_t>(id, add, std::int64_t{0}, pool_tree);
    };
}

TEST_CASE("allocator: teardown a tree under default vs monotonic vs pool", "[allocator][teardown][!benchmark]") {
    const Range        whole{0, kTeardownNodes};
    const std::int64_t expected = expected_sum(kTeardownNodes);

    const auto native = build_native_range(whole);

    // Law first, once, at this section's own (smaller) tree size: the
    // build and fold sections above already prove this at scale.
    REQUIRE(fold_map<std::int64_t>(id, add, std::int64_t{0}, to_fix(native)) == expected);
    REQUIRE(fold_map<std::int64_t>(id, add, std::int64_t{0}, build_mono(native).tree) == expected);
    REQUIRE(fold_map<std::int64_t>(id, add, std::int64_t{0}, build_pool(native).tree) == expected);

    // Each row below builds meter.runs() trees in an untimed setup phase,
    // then times only their destruction via Chronometer::measure —
    // Catch2's storage_for/destructable_object idiom for isolating
    // destruction from construction (catch_constructor.hpp).

    BENCHMARK_ADVANCED("teardown Fix<Box> (std::allocator)")(Catch::Benchmark::Chronometer meter) {
        const auto                                                 runs = static_cast<std::size_t>(meter.runs());
        std::vector<Catch::Benchmark::destructable_object<Fixed> > storage(runs);
        for (std::size_t i = 0; i != runs; ++i) {
            storage[i].construct(to_fix(native));
        }
        meter.measure([&](int i) { storage[static_cast<std::size_t>(i)].destruct(); });
    };

    BENCHMARK_ADVANCED("teardown Fix<Box> (monotonic_buffer_resource)")(Catch::Benchmark::Chronometer meter) {
        const auto                                                    runs = static_cast<std::size_t>(meter.runs());
        std::vector<std::pmr::monotonic_buffer_resource>              resources(runs);
        std::vector<Catch::Benchmark::destructable_object<PmrFixed> > storage(runs);
        for (std::size_t i = 0; i != runs; ++i) {
            pmr::allocator_type a(&resources[i]);
            storage[i].construct(pmr::to_fix(a, native));
        }
        meter.measure([&](int i) { storage[static_cast<std::size_t>(i)].destruct(); });
    };

    BENCHMARK_ADVANCED("teardown Fix<Box> (unsynchronized_pool_resource)")(Catch::Benchmark::Chronometer meter) {
        const auto                                                    runs = static_cast<std::size_t>(meter.runs());
        std::vector<std::pmr::unsynchronized_pool_resource>           resources(runs);
        std::vector<Catch::Benchmark::destructable_object<PmrFixed> > storage(runs);
        for (std::size_t i = 0; i != runs; ++i) {
            pmr::allocator_type a(&resources[i]);
            storage[i].construct(pmr::to_fix(a, native));
        }
        meter.measure([&](int i) { storage[static_cast<std::size_t>(i)].destruct(); });
    };

    BENCHMARK_ADVANCED("teardown shared_ptr tree (new/delete baseline)")(Catch::Benchmark::Chronometer meter) {
        const auto runs = static_cast<std::size_t>(meter.runs());
        std::vector<Catch::Benchmark::destructable_object<BinaryTree<std::int64_t> > > storage(runs);
        for (std::size_t i = 0; i != runs; ++i) {
            storage[i].construct(build_native_range(whole));
        }
        meter.measure([&](int i) { storage[static_cast<std::size_t>(i)].destruct(); });
    };

    BENCHMARK_ADVANCED("teardown unique_ptr tree (new/delete baseline)")(Catch::Benchmark::Chronometer meter) {
        const auto runs = static_cast<std::size_t>(meter.runs());
        std::vector<Catch::Benchmark::destructable_object<std::unique_ptr<bench::UniqueTree> > > storage(runs);
        for (std::size_t i = 0; i != runs; ++i) {
            storage[i].construct(build_unique(whole));
        }
        meter.measure([&](int i) { storage[static_cast<std::size_t>(i)].destruct(); });
    };
}
