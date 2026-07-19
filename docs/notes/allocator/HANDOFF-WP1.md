# WP-1 handoff — Allocator-aware `Box`

## Landed
- `include/beman/tree_algorithms/box.hpp` — `Box<A, Allocator = std::allocator<A>>`; allocation through `allocator_traits`; `leads_with_allocator_arg<Args...>()` guard helper; allocator-tagged `make_box`; default `make_box` constrained off the tag.
- `tests/beman/tree_algorithms/box.test.cpp` — POC matrix, allocation balance + SOCCC, pmr propagation, size/type static_asserts.
- `docs/DECISIONS.md` — Decision 9 (allocator model) landed (WP-0 gate: DECISIONS before code).
- Module interface: no change needed — `box.hpp` is already pulled into `tree_algorithms.hpp` (the umbrella the `.cppm` exports); no new header was added.

## Interface decided
- `Box<A, Allocator = std::allocator<A>>`. Members: public `[[no_unique_address]] Allocator alloc{};` and `A* ptr = nullptr;` (both kept public — `ptr` is relied on by tests and the transparent-value style). `allocator_type`, `get_allocator()`.
- Constructors downstream will use:
  - `Box<A, Alloc>(std::allocator_arg, alloc, std::in_place, args...)` — allocator-extended value ctor (uses-allocator-constructs the element).
  - `Box<A, Alloc>(std::allocator_arg, alloc)` — allocator-extended disengaged.
  - `Box<A>(A*)` and `Box<A>()` unchanged (default-allocator).
- `make_box<A>(args...)` — unchanged, default allocator.
- `make_box<A>(std::allocator_arg, alloc, args...)` — returns `Box<A, rebind_alloc<A>>`; **allocator_arg_t-tag-first**.
- **`leads_with_allocator_arg<Args...>()`** lives in `box.hpp`, `namespace beman::tree_algorithms`. This is the shared guard the handoff-WP0 note flagged: **every variadic factory downstream (WP-2 make_slot, WP-4 rose, WP-5 smart ctors) must reuse it**, constraining its default overload with `requires(!leads_with_allocator_arg<Args...>())`. Do not re-roll it.
- Element construction uses `std::uninitialized_construct_using_allocator(ptr, alloc, args...)` — this is what threads the allocator into an allocator-aware element and degrades to plain construction otherwise. Reuse this idiom, not `alloc.construct`.

## Verified
- `make compile` clean (no warnings), `make test` = **81/81** (was 77; +4 new Box cases), `make compile-headers` green (box.hpp self-contained).
- Spot-run `ctest -R '^Box'` = 7/7. POC matrix asserts each cell distinctly (adopt vs keep allocator id; steal vs reallocate). `stats.live() == 0` proves allocate==deallocate in every scenario. `Box<pmr::vector<int>>` over a `monotonic_buffer_resource`: the inner vector's `resource()` is ours.
- `sizeof(Box<int>) == sizeof(int*)` and `Box<int, std::allocator<int>>` is `Box<int>` — asserted.
- All 77 pre-existing tests pass unmodified (backward compatibility proven).

## Deviations
- **DEV-A02**: WP-1's file list names "module interface" as an expected touch. No edit was needed — the module exports the umbrella `tree_algorithms.hpp`, which already includes `box.hpp`, so folding is automatic for an existing header. No new header was introduced. (Non-departure; recorded because the file list anticipated a change that turned out unnecessary.)

## Open questions / warnings for downstream
- `Box(A*)` (raw-pointer adopt) assumes a default-constructed `Allocator` will free the pointer; it is default-allocator-only by contract. Don't use it with a stateful allocator.
- Move-assignment `noexcept` is conditional on `POCMA || is_always_equal` — true for `std::allocator`, so unchanged there; the unequal-non-propagating branch may allocate and is (correctly) not noexcept for stateful unequal allocators.
- WP-2: `child_slot<A, Allocator = std::allocator<A>>` must default so `child_slot_t<A>` (one-arg) is untouched; the knot rebinds `Allocator` to `Fix<F>`. `make_slot`'s tagged overload uses the `Box(allocator_arg, alloc, in_place, args...)` ctor at the knot and `uninitialized_construct_using_allocator`/`make_obj_using_allocator` on the inline optional branch.
