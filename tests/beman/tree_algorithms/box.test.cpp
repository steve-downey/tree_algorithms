// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/tree_algorithms/box.hpp>
#include <beman/tree_algorithms/box.hpp> // Re-inclusion: verifies include guard

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <memory>
#include <memory_resource>
#include <string>
#include <type_traits>
#include <vector>

using beman::tree_algorithms::Box;
using beman::tree_algorithms::make_box;

static_assert(std::is_default_constructible_v<Box<int>>);

// The stateless default allocator costs nothing: a Box is exactly a
// pointer, and its allocator_type is std::allocator<A> (D-A1 / Decision 9).
static_assert(sizeof(Box<int>) == sizeof(int*),
              "[[no_unique_address]] must erase the default (stateless) allocator");
static_assert(std::is_same_v<Box<int>::allocator_type, std::allocator<int>>);
static_assert(std::is_same_v<Box<int, std::allocator<int>>, Box<int>>,
              "the Allocator parameter must default to std::allocator so Box<A> is unchanged");

namespace {

// Box is aggregate-friendly: a struct holding a Box stays an aggregate and
// can be aggregate-initialised (nullable default, no explicit default ctor).
struct BoxAggregate {
    Box<int> boxed;
    int      tag = 0;
};

static_assert(std::is_aggregate_v<BoxAggregate>);
static_assert(BoxAggregate{}.tag == 0);
static_assert(BoxAggregate{}.boxed.ptr == nullptr);
static_assert(BoxAggregate{Box<int>{}, 3}.tag == 3);

constexpr auto box_deep_copy_roundtrip() -> bool {
    auto     b1 = make_box<int>(7);
    Box<int> b2 = b1;
    bool     ok = (*b1 == *b2) && (b1 == b2);
    *b2         = 99;
    ok          = ok && (*b1 == 7) && (*b2 == 99) && !(b1 == b2);
    return ok;
}

static_assert(box_deep_copy_roundtrip());

} // namespace

namespace {

// A stateful, counting allocator whose propagation traits are template
// parameters, so one type exercises every cell of the POC matrix. Equality
// is by id, so two instances can be made unequal on purpose.
struct alloc_stats {
    int allocations   = 0;
    int deallocations = 0;
    constexpr auto live() const -> int { return allocations - deallocations; }
};

template <typename T, bool POCCA, bool POCMA>
struct tracking_alloc {
    using value_type                             = T;
    using propagate_on_container_copy_assignment = std::bool_constant<POCCA>;
    using propagate_on_container_move_assignment = std::bool_constant<POCMA>;
    using is_always_equal                        = std::false_type;

    // The allocator select_on_container_copy_construction hands to a copy —
    // a distinct sentinel id, proving SOCCC was consulted (as std::pmr's
    // "return default" does).
    static constexpr int soccc_id = 999;

    alloc_stats* stats = nullptr;
    int          id    = 0;

    tracking_alloc(alloc_stats* s, int i) : stats(s), id(i) {}
    template <typename U>
    tracking_alloc(const tracking_alloc<U, POCCA, POCMA>& o) : stats(o.stats), id(o.id) {}

    template <typename U>
    struct rebind {
        using other = tracking_alloc<U, POCCA, POCMA>;
    };

    auto allocate(std::size_t n) -> T* {
        stats->allocations += 1;
        return std::allocator<T>{}.allocate(n);
    }
    void deallocate(T* p, std::size_t n) {
        stats->deallocations += 1;
        std::allocator<T>{}.deallocate(p, n);
    }
    auto select_on_container_copy_construction() const -> tracking_alloc { return tracking_alloc(stats, soccc_id); }

    friend auto operator==(const tracking_alloc& a, const tracking_alloc& b) -> bool { return a.id == b.id; }
};

template <bool POCCA, bool POCMA>
using IntBox = Box<int, tracking_alloc<int, POCCA, POCMA>>;

} // namespace

TEST_CASE("Box - AllocationBalanceAndSOCCC", "[tree_algorithms::box]") {
    alloc_stats stats;
    {
        tracking_alloc<int, false, false> a(&stats, 1);
        IntBox<false, false>              b1(std::allocator_arg, a, std::in_place, 7);
        CHECK(*b1 == 7);
        CHECK(b1.get_allocator().id == 1);
        CHECK(stats.allocations == 1);

        // Copy construction consults SOCCC for the copy's allocator.
        IntBox<false, false> b2 = b1;
        CHECK(*b2 == 7);
        CHECK(b2.get_allocator().id == tracking_alloc<int, false, false>::soccc_id);
        CHECK(stats.allocations == 2);

        *b2 = 99; // deep copy: independent storage
        CHECK(*b1 == 7);
    }
    CHECK(stats.live() == 0); // every allocation was balanced by a deallocation
    CHECK(stats.allocations == 2);
    CHECK(stats.deallocations == 2);
}

TEST_CASE("Box - CopyAssignPOCCA", "[tree_algorithms::box]") {
    alloc_stats stats;
    {
        SECTION("propagate: target adopts the source allocator") {
            IntBox<true, true> dst(std::allocator_arg, tracking_alloc<int, true, true>(&stats, 1), std::in_place, 1);
            IntBox<true, true> src(std::allocator_arg, tracking_alloc<int, true, true>(&stats, 2), std::in_place, 2);
            dst = src;
            CHECK(*dst == 2);
            CHECK(dst.get_allocator().id == 2); // POCCA true -> adopted
        }
        SECTION("don't propagate: target keeps its own allocator") {
            IntBox<false, false> dst(std::allocator_arg, tracking_alloc<int, false, false>(&stats, 1), std::in_place, 1);
            IntBox<false, false> src(std::allocator_arg, tracking_alloc<int, false, false>(&stats, 2), std::in_place, 2);
            dst = src;
            CHECK(*dst == 2);
            CHECK(dst.get_allocator().id == 1); // POCCA false -> kept
        }
    }
    CHECK(stats.live() == 0);
}

TEST_CASE("Box - MoveAssignPOCMA", "[tree_algorithms::box]") {
    alloc_stats stats;
    {
        SECTION("propagate: adopt source allocator and steal storage") {
            IntBox<true, true> dst(std::allocator_arg, tracking_alloc<int, true, true>(&stats, 1), std::in_place, 1);
            IntBox<true, true> src(std::allocator_arg, tracking_alloc<int, true, true>(&stats, 2), std::in_place, 2);
            int                allocs_before = stats.allocations;
            dst                              = std::move(src);
            CHECK(*dst == 2);
            CHECK(dst.get_allocator().id == 2);
            CHECK(stats.allocations == allocs_before); // stolen, not reallocated
            CHECK_FALSE(static_cast<bool>(src));
        }
        SECTION("don't propagate, equal allocators: steal storage") {
            IntBox<false, false> dst(std::allocator_arg, tracking_alloc<int, false, false>(&stats, 5), std::in_place, 1);
            IntBox<false, false> src(std::allocator_arg, tracking_alloc<int, false, false>(&stats, 5), std::in_place, 2);
            int                  allocs_before = stats.allocations;
            dst                                = std::move(src);
            CHECK(*dst == 2);
            CHECK(dst.get_allocator().id == 5);
            CHECK(stats.allocations == allocs_before); // equal -> stolen
            CHECK_FALSE(static_cast<bool>(src));
        }
        SECTION("don't propagate, unequal allocators: element-wise move") {
            IntBox<false, false> dst(std::allocator_arg, tracking_alloc<int, false, false>(&stats, 1), std::in_place, 1);
            IntBox<false, false> src(std::allocator_arg, tracking_alloc<int, false, false>(&stats, 2), std::in_place, 2);
            int                  allocs_before = stats.allocations;
            dst                                = std::move(src);
            CHECK(*dst == 2);
            CHECK(dst.get_allocator().id == 1);            // kept its own
            CHECK(stats.allocations == allocs_before + 1); // reallocated in dst's arena
        }
    }
    CHECK(stats.live() == 0);
}

TEST_CASE("Box - PropagatesIntoAllocatorAwareElement", "[tree_algorithms::box]") {
    // The load-bearing claim (D-A1 / Decision 9): a nested allocator-aware
    // element receives this Box's allocator by uses-allocator construction.
    std::pmr::monotonic_buffer_resource pool;
    using PVec = std::pmr::vector<int>;
    std::pmr::polymorphic_allocator<PVec> a(&pool);

    auto b = make_box<PVec>(std::allocator_arg, a, std::initializer_list<int>{1, 2, 3});
    CHECK(b->size() == 3);
    CHECK(b->get_allocator().resource() == &pool); // the vector allocated from our resource
    CHECK(b.get_allocator().resource() == &pool);
}

TEST_CASE("Box - MakeBoxInt", "[tree_algorithms::box]") {
    auto b = make_box<int>(42);
    CHECK(*b == 42);
}

TEST_CASE("Box - MakeBoxString", "[tree_algorithms::box]") {
    auto b = make_box<std::string>("hello");
    CHECK(*b == "hello");
}

TEST_CASE("Box - DeepCopyOnCopy", "[tree_algorithms::box]") {
    auto     b1 = make_box<int>(7);
    Box<int> b2 = b1;
    CHECK(*b1 == *b2);
    *b2 = 99;
    CHECK(*b1 == 7);
    CHECK(*b2 == 99);
}
