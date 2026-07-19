// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_TREE_ALGORITHMS_BOX_HPP
#define BEMAN_TREE_ALGORITHMS_BOX_HPP

#include <beman/tree_algorithms/config.hpp>

#if BEMAN_TREE_ALGORITHMS_USE_MODULES() && !defined(BEMAN_TREE_ALGORITHMS_INCLUDED_FROM_INTERFACE_UNIT)

import beman.tree_algorithms;

#else

    #if !BEMAN_TREE_ALGORITHMS_USE_MODULES()
        #include <memory>
        #include <tuple>
        #include <type_traits>
        #include <utility>
    #endif

namespace beman::tree_algorithms {

// f2c4e1a7-6d0b-4e59-8c2a-7b1e0f3d9a54
/** True when the first of @p Args is std::allocator_arg_t.
 *
 * A variadic factory that also offers an allocator_arg_t-tagged overload
 * is otherwise ambiguous with it, and the compiler silently prefers the
 * un-tagged form — swallowing `allocator_arg, alloc` as ordinary
 * constructor arguments. Every variadic factory here (make_box,
 * make_slot, the smart constructors) constrains its default overload with
 * `!leads_with_allocator_arg<Args...>()` so the tagged overload is the
 * only match when a tag is present. Fixed-arity verbs need no such guard;
 * they take a trailing allocator instead. */
template <typename... Args>
constexpr auto leads_with_allocator_arg() -> bool {
    if constexpr (sizeof...(Args) == 0)
        return false;
    else
        return std::is_same_v<std::remove_cvref_t<std::tuple_element_t<0, std::tuple<Args...> > >,
                              std::allocator_arg_t>;
}
// f2c4e1a7-6d0b-4e59-8c2a-7b1e0f3d9a54 end

/** Constexpr-capable owning pointer with deep-copy value semantics.
 * Nullable (default ptr=nullptr), so it works in static_vector storage
 * without wasteful default-allocations. Allocates through
 * allocator_traits, which is constexpr in C++20+ for transient
 * allocations under the default std::allocator.
 *
 * This is a workaround until consteval allocation lands in a future
 * standard; std::indirect has the right semantics but its explicit
 * default constructor blocks use in aggregate-initialised containers.
 *
 * Allocator awareness is additive: with the default std::allocator the
 * stored allocator occupies no bytes ([[no_unique_address]]), and size,
 * cost, and constexpr behavior are exactly what they were before the
 * Allocator parameter existed. A stateful allocator (e.g. pmr) costs one
 * pointer per Box — one per heap edge — and is propagated to the owned A
 * by uses-allocator construction, so a nested allocator-aware element
 * receives this Box's allocator. State-carrying allocators honor the
 * container rules: select_on_container_copy_construction on copy,
 * allocator stolen on move, POCCA/POCMA on assignment with an
 * element-wise fallback when the traits say don't-propagate and the
 * allocators are unequal.
 *
 * @tparam A the wrapped type (typically a recursive Fix instantiation)
 * @tparam Allocator allocator for A; std::allocator<A> by default (empty)
 */
// a1f81cb9-ebb3-4872-9549-03ca140c61b2
template <typename A, typename Allocator = std::allocator<A> >
struct Box {
    using allocator_type = Allocator;

  private:
    using alloc_traits = std::allocator_traits<Allocator>;

    /** Allocate one A and construct it in place by uses-allocator
     * construction, so a nested allocator-aware A is handed this Box's
     * allocator; degrades to plain construction otherwise. */
    template <typename... Args>
    constexpr auto create(Args&&... args) -> A* {
        A* p = alloc_traits::allocate(alloc, 1);
        std::uninitialized_construct_using_allocator(p, alloc, std::forward<Args>(args)...);
        return p;
    }

    /** Destroy and free the owned A (if any), leaving the Box disengaged. */
    constexpr void dispose() {
        if (ptr) {
            alloc_traits::destroy(alloc, ptr);
            alloc_traits::deallocate(alloc, ptr, 1);
            ptr = nullptr;
        }
    }

  public:
    [[no_unique_address]] Allocator alloc{};
    A*                              ptr = nullptr;

    constexpr Box() = default;

    /** Allocator-extended default constructor: a disengaged Box that will
     * allocate through @p a. */
    constexpr Box(std::allocator_arg_t, const Allocator& a) : alloc(a) {}

    /** Adopt a raw pointer (default-allocator only: the destructor frees
     * it through a default-constructed Allocator). */
    constexpr explicit Box(A* p) : ptr(p) {}

    /** Allocator-extended value constructor: own a fresh A built from
     * @p args by uses-allocator construction through @p a. */
    template <typename... Args>
    constexpr Box(std::allocator_arg_t, const Allocator& a, std::in_place_t, Args&&... args)
        : alloc(a), ptr(create(std::forward<Args>(args)...)) {}

    constexpr Box(const Box& other)
        : alloc(alloc_traits::select_on_container_copy_construction(other.alloc)),
          ptr(other.ptr ? create(*other.ptr) : nullptr) {}

    constexpr auto operator=(const Box& other) -> Box& {
        if (this != &other) {
            dispose();
            if constexpr (alloc_traits::propagate_on_container_copy_assignment::value) {
                alloc = other.alloc;
            }
            ptr = other.ptr ? create(*other.ptr) : nullptr;
        }
        return *this;
    }

    constexpr Box(Box&& other) noexcept : alloc(std::move(other.alloc)), ptr(std::exchange(other.ptr, nullptr)) {}

    constexpr auto operator=(Box&& other) noexcept(alloc_traits::propagate_on_container_move_assignment::value ||
                                                   alloc_traits::is_always_equal::value) -> Box& {
        if (this != &other) {
            dispose();
            if constexpr (alloc_traits::propagate_on_container_move_assignment::value) {
                alloc = std::move(other.alloc);
                ptr   = std::exchange(other.ptr, nullptr);
            } else if (alloc == other.alloc) {
                ptr = std::exchange(other.ptr, nullptr);
            } else {
                // Unequal, non-propagating: cannot steal the storage — it
                // belongs to other's allocator. Move the element across
                // into storage of our own, then release other's.
                ptr = other.ptr ? create(std::move(*other.ptr)) : nullptr;
                other.dispose();
            }
        }
        return *this;
    }

    constexpr ~Box() { dispose(); }

    constexpr auto operator*() const -> A& { return *ptr; }
    constexpr auto operator->() const -> A* { return ptr; }

    /** Engaged test, matching std::optional's contextual conversion so
     * child_slot code reads the same whether the slot is boxed or inline. */
    constexpr explicit operator bool() const { return ptr != nullptr; }

    /** The allocator this Box builds and frees its element through. */
    constexpr auto get_allocator() const -> allocator_type { return alloc; }

    friend constexpr auto operator==(const Box& lhs, const Box& rhs) -> bool {
        if (lhs.ptr == rhs.ptr)
            return true;
        if (!lhs.ptr || !rhs.ptr)
            return false;
        return *lhs.ptr == *rhs.ptr;
    }
};
// a1f81cb9-ebb3-4872-9549-03ca140c61b2 end

// 45a64b08-8217-4baf-b19b-146434de1e44
/** Construct a Box<A>, forwarding @p args to A's constructor. */
template <typename A, typename... Args>
    requires(!leads_with_allocator_arg<Args...>())
constexpr auto make_box(Args&&... args) -> Box<A> {
    return Box<A>(std::allocator_arg, std::allocator<A>{}, std::in_place, std::forward<Args>(args)...);
}

/** Construct a Box<A> whose element and storage use @p a, rebinding it to
 * A. The allocator is threaded into the element by uses-allocator
 * construction. Spelled allocator_arg_t-tag-first (a trailing allocator
 * would be indistinguishable from a forwarded constructor argument). */
template <typename A, typename Allocator, typename... Args>
constexpr auto make_box(std::allocator_arg_t, const Allocator& a, Args&&... args)
    -> Box<A, typename std::allocator_traits<Allocator>::template rebind_alloc<A> > {
    using Rebound = typename std::allocator_traits<Allocator>::template rebind_alloc<A>;
    return Box<A, Rebound>(std::allocator_arg, Rebound(a), std::in_place, std::forward<Args>(args)...);
}
// 45a64b08-8217-4baf-b19b-146434de1e44 end

} // namespace beman::tree_algorithms

#endif // BEMAN_TREE_ALGORITHMS_USE_MODULES() &&
       // !defined(BEMAN_TREE_ALGORITHMS_INCLUDED_FROM_INTERFACE_UNIT)

#endif // BEMAN_TREE_ALGORITHMS_BOX_HPP
