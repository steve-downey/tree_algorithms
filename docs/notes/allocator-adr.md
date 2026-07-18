# ADR — Allocator model for `beman::tree_algorithms`

Status: PROPOSED (WP-0 output; awaiting managing-agent / Steve review before WP-1 dispatches).
Companion to `docs/notes/allocator-awareness-plan.md`; validates that plan's §3 decisions D-A1…D-A6 with a compiled spike.
This ADR records what the spike proved, the one spelling hazard it surfaced, and the exact interface downstream work packages must build on.

## Context

The Fix-based trees own their elements, have value semantics, and allocate — they are very nearly containers.
The plan proposes finishing the container contract: uses-allocator construction throughout, and a `pmr::` surface.
WP-0's job is not to design that from scratch — the plan already decided the shape in D-A1…D-A6 — but to *falsify or confirm* those decisions with throwaway code before any production code is written, and to nail down the one thing the plan left to the spike: the allocator-parameter spelling for factories (plan §3, D-A1(e)).

## The spike

Throwaway, not landed (WP-0 is explicitly out of scope for production code).
Kept at `scratchpad/wp0_spike.cpp` for the review; compiled standalone with the repository's exact flags rather than through `make compile`, since it must not enter the build tree:

```
/usr/bin/c++ -Wall -Wextra -std=gnu++23 -fconstexpr-ops-limit=1000000000 -O2 wp0_spike.cpp
```

Toolchain: gcc 15.2 (Ubuntu), i.e. within the Decision-3 floor (gcc-14+/clang-18+, C++23).
It prototypes an allocator-aware `Box`, a rebinding `child_slot`, allocator-tagged `make_box`/`make_slot`, and an aggregate `AddF`/`ExprF` layer, then asserts the six claims below.

## Claims validated

| # | Claim (plan D-A reference) | How proved | Result |
|---|---|---|---|
| a | `Box<A, std::allocator<A>>` stays fully constexpr through `allocator_traits` allocate/construct/destroy/deallocate (D-A1) | `static_assert(box_roundtrip() == 42)` — make/copy/move/destroy at compile time | PASS |
| b | `[[no_unique_address]]` erases the stateless allocator: `sizeof(Box<int>) == sizeof(int*)` (D-A1) | `static_assert(sizeof(Box<int>) == sizeof(int*))` | PASS |
| c | `make_obj_using_allocator` threads the allocator into an allocator-aware element (D-A1, D-A6) | runtime: `Box<pmr::vector>` built over a `monotonic_buffer_resource`; the vector's `get_allocator().resource()` is our resource | PASS |
| d | Aggregate layer literals still compile with the extra `Box` allocator parameter (D-A2) | `static_assert(aggregate_literal_compiles())` over a braced `ExprF` layer | PASS |
| e | Allocator-tagged `make_slot` inline (optional) branch stays constexpr with `std::allocator` (D-A3, WP-2 gate) | `static_assert(tagged_make_slot_inline() == 8)` | PASS |
| — | Program run confirms (c) at runtime | `nested_propagation = true (resource threaded)` | PASS |

None of D-A1…D-A6 was falsified. The plan's design stands.

## The one hazard the spike surfaced — spelling decision (e)

The plan left open (D-A1(e)) whether factories take the allocator as an `std::allocator_arg_t` tag-first argument or as a trailing parameter. The spike settles it, and in doing so caught a silent-miscompile trap:

**Finding.** A variadic default factory (`make_box<A>(Args&&...)`) and an allocator-tagged overload (`make_box<A>(allocator_arg_t, const Alloc&, Args&&...)`) are **ambiguous on partial ordering, and GCC silently resolves to the default overload** — swallowing `allocator_arg, alloc` as ordinary constructor arguments. The first symptom is a deep `is_constructible` static_assert failure inside `<bits/uses_allocator_args.h>`, not at the call site. A trailing allocator parameter is worse still: in a variadic it is indistinguishable from a forwarded constructor argument.

**Resolution (binding on WP-1…WP-7):**

1. **Factories with variadic argument packs** — `make_box`, `make_slot`, and any smart constructor forwarding a pack — take the allocator **`allocator_arg_t`-tag-first**: `make_box<A>(std::allocator_arg, alloc, args...)`. This mirrors the standard's own uses-allocator construction convention.
2. **The default (un-tagged) overload must be constrained to reject a leading `allocator_arg_t`**, or the ambiguity above recurs. The spike uses:
   ```cpp
   template <typename... Args>
   constexpr bool leads_with_alloc_arg() {
       if constexpr (sizeof...(Args) == 0) return false;
       else return std::is_same_v<
           std::remove_cvref_t<std::tuple_element_t<0, std::tuple<Args...>>>,
           std::allocator_arg_t>;
   }
   // template <typename A, typename... Args>
   //     requires(!leads_with_alloc_arg<Args...>())
   // auto make_box(Args&&...) -> Box<A>;
   ```
   A shared `not_allocator_tagged` helper (name TBD in WP-1) will live next to `Box` and be reused by every variadic factory.
3. **Verbs with fixed arity** — `unfold_fix`, `refold`, `to_fix`, `from_fix` — take the allocator as a **trailing parameter**, per the plan's D-A6 ("a trailing parameter, never a leading template parameter"). Fixed arity makes this unambiguous; it keeps the Decision-4 verb shape intact and reads naturally at the call site.

The split (tag-first for variadic factories, trailing for fixed-arity verbs) is deliberate and each half is chosen for disambiguation, not taste.

## Decisions carried forward for downstream WPs (the interface)

- **`Box<A, Allocator = std::allocator<A>>`** — `allocator_type`, `[[no_unique_address]]` stored allocator, all allocation/construction through `std::allocator_traits`, element constructed with `std::make_obj_using_allocator` so nested allocator-aware `A` receives the allocator. `select_on_container_copy_construction` on copy; move steals; POCCA/POCMA honored on assignment with an unequal-allocator element-wise fallback; `get_allocator()`. Default-allocator size, cost, and constexpr behavior are bit-for-bit today's.
- **`child_slot<A, Allocator = std::allocator<A>>`** — inline `std::optional<A>` at complete types (unchanged in type); at the knot `Box<Fix<F>, rebind_alloc<Fix<F>>>`. The partial specialization gains the `Allocator` parameter. `child_slot_t<A, Allocator = std::allocator<A>>`.
- **`make_slot`** — existing 1-arg spelling unchanged; new `make_slot<A>(std::allocator_arg, alloc, args...)` overload, rebinding at the knot and uses-allocator-constructing the inline element.
- **Layer types stay aggregates** (D-A2). No `uses_allocator` specialization for `Fix` or layer types; the protocol members live only on `Box` and container-like types.
- **Verbs**: allocator as trailing parameter; `fold_fix` unchanged in signature but the tagged `make_slot` is available to algebras.

## Consequences

- Zero behavior/size/constexpr change on the default `std::allocator` path — the additivity constraint (§2) is satisfiable.
- One pointer per heap edge for a stateful allocator (`pmr`), exactly the per-node cost a pmr node-based container pays — to be stated candidly in the paper (WP-8).
- The constrained-default-overload pattern is a new, small piece of shared vocabulary every variadic factory must adopt; WP-1 owns its final name and home.

## Draft Decision 9 for `DECISIONS.md` (for review; lands before WP-1 per the WP-0 gate)

> ## Decision 9 — Allocator model: edges carry the allocator, propagated by construction
>
> `beman::tree_algorithms` is allocator-aware under the uses-allocator protocol, additively: every existing spelling keeps its current meaning, cost, size, and constexpr capability on the default `std::allocator` path.
>
> - **No container to own an allocator.** `Fix<F>` is a bare recursive value, so the owning *edges* carry the allocator: `Box<A, Allocator = std::allocator<A>>` stores it via `[[no_unique_address]]` (zero bytes for stateless allocators — `sizeof(Box<int>) == sizeof(int*)` is asserted). Allocator flow is scoped-by-construction: an allocator supplied at a construction site is threaded to children through uses-allocator construction (`std::make_obj_using_allocator`), exactly as `std::pmr` containers propagate a resource. State-carrying edges honor the container rules: `select_on_container_copy_construction` on copy, allocator stolen on move, POCCA/POCMA on assignment with an element-wise fallback when traits say don't-propagate and allocators are unequal, POCS on swap.
> - **Layer types stay aggregates.** `BinaryTreeF`, `ExprF`, `RoseF`, and friends remain aggregates; braced-init of layers is preserved. Allocators enter through the factory/verb surface, not through allocator-extended constructors. Consequently `std::uses_allocator` is *not* specialized for `Fix` or the layer types; the protocol members appear only on the types that actually store an allocator (`Box`, and container-like types such as `BinaryTree`). This is a deliberate departure from "every class gets `allocator_type`"; the paper presents and defends it.
> - **Spelling.** Variadic factories (`make_box`, `make_slot`, smart constructors) take the allocator `allocator_arg_t`-tag-first, and their default overloads are constrained to reject a leading `allocator_arg_t` (a bare overload is ambiguous and silently mis-resolves). Fixed-arity verbs (`unfold_fix`, `refold`, `to_fix`, `from_fix`) take a trailing allocator parameter; the Decision-4 verb shape is unchanged.
> - **`pmr` surface.** A `beman::tree_algorithms::pmr` namespace of aliases and allocator-bound factories, bound to `std::pmr::polymorphic_allocator`; runtime-only, and it says so — the constexpr contract (Decision 7, DEV-04) is unaffected on the default path.
> - **`shared_ptr`-spine representations** (`BinaryTree`, fringe tree) use `std::allocate_shared`; the control block remembers the allocator, so no per-node storage is added.
>
> Everything in Decisions 1–8 stands. This decision finishes the container contract begun by the value-semantics/`Box` decisions (3, 6) and the storage-at-the-knot amendment (2026-07-18).
