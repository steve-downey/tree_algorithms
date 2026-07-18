# Allocator Awareness for `beman::tree_algorithms` — Execution Plan

Status: PLANNED — not yet started.
Audience: the managing agent supervising this campaign, and the sub-agents it dispatches.
This document is the contract: work packages, dependencies, acceptance gates, and the reporting protocol.
Deviations from this plan are permitted but must be recorded (see §7) — never silent.

## 1. Goal and thesis

The Fix-based trees in this repository are very nearly proper containers: they own their elements, they have value semantics, they allocate.
A standard-track proposal owes them the rest of the container contract: allocator awareness under the uses-allocator protocol, and `pmr::` variants.
The recent representation round showed Fix-based trees can beat hand-rolled types in some cases; allocator support (monotonic and pool resources for tree build/teardown) is where that advantage should widen, since per-node `new`/`delete` at the knot is the dominant cost of tree construction.

Deliverables:

1. Allocator-aware `Box`, `child_slot`/`make_slot`, and `Fix` plumbing (uses-allocator construction throughout).
2. Allocator-aware representations: rose tree, `BinaryTree` + `BinaryTreeF`, `ExprF`, fringe tree.
3. Allocator-extended verb overloads where the verbs allocate (`unfold_fix`, `refold`, `to_fix`, `from_fix`).
4. A `beman::tree_algorithms::pmr` surface: aliases and factories bound to `std::pmr::polymorphic_allocator`.
5. Tests proving propagation (tracking allocator + `pmr` resource assertions) without regressing the constexpr contract.
6. Benchmarks quantifying monotonic/pool resources vs default `new`/`delete` vs the naive hand-rolled trees.
7. Paper and `DECISIONS.md` updates recording the allocator model.

## 2. Standing constraints (binding on every sub-agent)

- **`docs/DECISIONS.md` is frozen input.** Nothing here relitigates Decisions 1–8. In particular: value semantics and deep-copying `Box` stay (Decision 6); `Box` stays hand-rolled, not `std::indirect` (Decision 3); naming stays `fold_fix`/`unfold_fix`/`refold` (Decision 2); C++23 floor, gcc-14+/clang-18+ (Decision 3).
- **Allocator awareness must be additive.** Every existing spelling — `Box<A>`, `child_slot_t<A>`, `make_slot<A>(...)`, `wrap_fix`, aggregate braced-init of layer types — must keep compiling and keep its current meaning and cost. Default allocator = `std::allocator`, stored at zero size.
- **The constexpr contract is non-negotiable.** Everything currently constexpr stays constexpr with the default allocator, enforced by the existing `static_assert` tests (DEV-04). The `pmr` surface is runtime-only and says so.
- **Testing rules of Decision 7 apply to all new tests**: non-idempotent order-sensitive algebras where verbs are exercised (DEV-01); no bare CTAD on wrapped types (DEV-03); constexpr `static_assert` coverage for constexpr-capable additions (DEV-04); Catch2; double-include + bootstrap test opening every new test file.
- **House style applies** (Beman flavor): canonical includes only, self-contained headers, classical include guards, out-of-line qualified definitions for non-template code, `FILE_SET HEADERS` in CMake, `snake_case` filenames, one sentence per line in prose. New public headers get UUID transclusion anchors around paper-worthy blocks.
- **`infra/` is a shared subtree — hands off.** Never edit anything under `infra/`; if wiring is wrong, fix it in this project's own CMake.
- **Build/verify via the Makefile recipes only**: `make compile`, `make test`, `make compile-headers` (header self-containment), benchmarks in the Bench config per repo convention. Do not hand-roll cmake invocations or add configs; configuration lives in toolchain flags.
- **Module build must stay green.** Any new header must be folded into `tree_algorithms.cppm` and `tree_algorithms.hpp`, following the existing `BEMAN_TREE_ALGORITHMS_USE_MODULES()` header pattern exactly.

## 3. Design decisions made by this plan

These are decided now so sub-agents do not re-derive them.
WP-0 validates them with a spike; if the spike falsifies one, that is a deviation to record and escalate, not a license to improvise.

### D-A1. Allocator lives in the edge, propagated by construction

There is no container object to own an allocator — `Fix<F>` is a bare recursive value.
So the owning edges carry it: `Box<A, Allocator>` stores its allocator (via `[[no_unique_address]]`), and allocator flow is **scoped-by-construction**: an allocator supplied at a construction site is passed down to children via uses-allocator construction, exactly as `std::pmr` containers propagate a resource.
For stateless `std::allocator` this is zero bytes and zero behavior change.
For `pmr::polymorphic_allocator` it is one pointer per `Box` — i.e., one pointer per heap edge, the same overhead a pmr node-based container pays per node.
State-carrying edges must honor the container rules: `select_on_container_copy_construction` on copy, allocator stolen on move, POCCA/POCMA consulted on assignment (with element-wise fallback when the traits say don't propagate and allocators are unequal), POCS on swap.

### D-A2. Layer types stay aggregates; allocators enter through the factories

`BinaryTreeF`, `ExprF`, `RoseF`, and friends are aggregates, and braced-init of layers is pervasive (and DEV-03 depends on naming the full type, not on constructors).
Adding allocator-extended constructors would destroy aggregate-ness and ripple through every layer literal in the codebase and the paper.
Decision: **layer types remain aggregates**.
Allocator injection happens at the factory/verb surface — `make_box`, `make_slot`, `wrap_fix`, `rose`, `unfold_fix`, `to_fix` — via allocator-taking overloads that perform uses-allocator construction of the children (`std::make_obj_using_allocator`, constexpr since C++20).
Consequence: `std::uses_allocator` is **not** specialized for layer types or for `Fix`; the protocol members (`allocator_type` etc.) appear only on the types that actually store an allocator (`Box`, and container-like types such as `BinaryTree`).
This is the deliberate deviation from "every class gets `allocator_type`" orthodoxy; the paper section (WP-8) must present and defend it.

### D-A3. `child_slot` grows an allocator parameter with a defaulted spelling

`child_slot<A, Allocator = std::allocator<A>>` / `child_slot_t<A, Allocator = ...>`:
Box at the knot becomes `Box<Fix<F>, rebound-Allocator>`; the inline `std::optional<A>` branch is unchanged in type (optional never allocates) but `make_slot` gains an `std::allocator_arg_t`-tagged overload that uses-allocator-constructs the element.
Existing one-argument spellings keep working unchanged.

### D-A4. The `pmr` surface is aliases + factories, in one new header

`include/beman/tree_algorithms/pmr.hpp` (or split per-representation if WP-0 finds that cleaner — record as deviation) provides, inside `namespace beman::tree_algorithms::pmr`:
aliases (`Box<A>`, `RoseTreeFix<T>` with `std::pmr::vector` children, `BinaryTreeFix<T>`, `ExprFix`) and allocator-bound factories mirroring `rose`, `make_slot`, the smart constructors, and `to_fix`.
The pmr rose layer is a distinct layer template (its children are `std::pmr::vector`), registered with the functor/layer-fold lookups like any other layer — this demonstrates the paper's claim that the verbs are representation-generic.

### D-A5. `shared_ptr`-spine types use `allocate_shared`

`BinaryTree<T>` (and the fringe tree) get allocator-extended static factories (`leaf(alloc, v)`, `node(alloc, ...)` or an `allocator_arg_t` convention — WP-0 picks one spelling and everyone follows it) built on `std::allocate_shared`.
The `shared_ptr` control block remembers its allocator, so no per-node storage is added; a stored `allocator_type` member is needed only if mutating operations allocate later (they don't — these trees are persistent).

### D-A6. Verbs that build get allocator-extended overloads; folds pass through

`unfold_fix` and `refold` allocate the result structure: they get overloads taking a trailing allocator that is threaded into `make_slot`/`wrap_fix` when materializing layers.
`fold_fix` allocates only inside user algebras and intermediate `F<Result>` layers whose slots are inline `optional` — no signature change, but the allocator-tagged `make_slot` must be available to algebras that want it.
Verb signatures keep their Decision 4 shape; the allocator is a trailing parameter, never a leading template parameter.

## 4. Work packages

Each WP lists: owner (one sub-agent), inputs, scope, out-of-scope, acceptance gate.
File paths are relative to repo root.
Every WP ends with the sub-agent writing its handoff note (§7) — that is part of the acceptance gate, not optional.

### WP-0 — Design spike and ADR (serial; everything else waits on it)

- **Scope**: Validate D-A1…D-A6 with throwaway code in a scratch branch of `box.hpp`/`child_slot.hpp`: confirm (a) `Box<A, std::allocator<A>>` stays fully constexpr through `allocator_traits` allocate/construct; (b) `[[no_unique_address]]` keeps `sizeof(Box<A>)` unchanged for the default allocator; (c) `make_obj_using_allocator` works constexpr through `make_slot` for allocator-aware element types; (d) aggregate layer literals still compile everywhere; (e) pick the single allocator-parameter spelling for factories (`std::allocator_arg_t` tag first vs trailing parameter) and write it down.
  Then write `docs/notes/allocator-adr.md` recording the validated decisions and the chosen spellings, and draft the new `DECISIONS.md` entry ("Decision 9 — Allocator model") for the supervisor to review.
- **Out of scope**: landing any production code.
- **Gate**: spike compiles under `make compile`; ADR reviewed by the managing agent **before any other WP is dispatched**. The `DECISIONS.md` entry lands first, per that file's own rule that changes are recorded there before code.

### WP-1 — Allocator-aware `Box` (serial, after WP-0)

- **Files**: `include/beman/tree_algorithms/box.hpp`, `tests/beman/tree_algorithms/box.test.cpp`, module interface.
- **Scope**: `Box<A, Allocator = std::allocator<A>>` with: `allocator_type`; `[[no_unique_address]]` stored allocator; allocation/construction via `allocator_traits` (uses-allocator construction of `A` so nested allocator-aware types receive the allocator); `select_on_container_copy_construction` on copy; move steals; POCCA/POCMA-honoring assignment with unequal-allocator element-copy fallback; `get_allocator()`; allocator-extended constructors; `make_box` overload taking an allocator.
  Default-allocator behavior, size, and constexpr capability must be bit-for-bit what they are today.
- **Out of scope**: `child_slot`, pmr aliases.
- **Gate**: `make test` green; new tests cover: propagation into an allocator-aware `A`, POC-trait matrix (all four copy/move × propagate/don't cases), tracking-allocator balance (allocate count == deallocate count), `static_assert` constexpr round-trip with default allocator, `sizeof(Box<int>) == sizeof(int*)` static_assert.

### WP-2 — `child_slot` / `make_slot` / `Fix` plumbing (serial, after WP-1)

- **Files**: `child_slot.hpp`, `fix.hpp`, their tests, module interface.
- **Scope**: D-A3 as specified; `make_slot` allocator overload using `make_obj_using_allocator` for the inline branch and the rebound `Box` for the knot; `wrap_fix` allocator-extended overload; verify `unwrap_fix` needs nothing.
- **Gate**: `make test` + `make compile-headers` green; constexpr static_asserts for the allocator-tagged `make_slot` with `std::allocator`; existing tests untouched and passing (backward-compat proof).

### WP-3 — Verb overloads (after WP-2)

- **Files**: `recursion_schemes.hpp`, `recursion_schemes_lookup.hpp`, tests.
- **Scope**: D-A6 — allocator-extended `unfold_fix`/`refold` (both explicit-`fmap` and lookup forms) threading the allocator into layer materialization.
- **Gate**: DEV-01-compliant tests (non-idempotent coalgebra) proving the built tree's `Box` edges hold the supplied allocator (tracking allocator sees every node); constexpr coverage with default allocator.

### WP-4 — Rose tree (parallel with WP-5/6 after WP-2)

- **Files**: `rose_tree.hpp`, test, and the pmr rose layer pieces it owns.
- **Scope**: `RoseF` children vector allocator-parameterized (defaulted, additive); `rose()` allocator overload; functor instance and layer fold generalized over the vector's allocator; groundwork so WP-7's `pmr::RoseTreeFix` is just an alias + registrations.
- **Gate**: existing rose tests pass unchanged; new test builds a rose tree into a tracking allocator and proves every vector allocation went through it.

### WP-5 — `BinaryTree`, `BinaryTreeF`, `ExprF` (parallel after WP-2)

- **Files**: `binary_tree.hpp`, `expression.hpp`, tests.
- **Scope**: D-A5 `allocate_shared` factories for `BinaryTree`; allocator-extended `to_fix`/`from_fix` (allocator for the Fix side; allocator for the rebuilt `shared_ptr` side); `ExprF` smart constructors gain allocator overloads via the WP-2 `make_slot`.
- **Gate**: tracking-allocator tests over `to_fix`/`from_fix` round trip; existing tests untouched and green.

### WP-6 — Fringe tree (parallel after WP-2)

- **Files**: `fringe_tree.hpp`, test.
- **Scope**: same treatment as WP-5's `BinaryTree` side: `allocate_shared` spine factories, allocator-extended conversions if it has them.
- **Gate**: as WP-5.

### WP-7 — `pmr` surface (after WP-3…WP-6)

- **Files**: new `include/beman/tree_algorithms/pmr.hpp` + `tests/beman/tree_algorithms/pmr.test.cpp`; CMake `FILE_SET` additions; module interface.
- **Scope**: D-A4. Also the flagship test pattern: build/fold/unfold each representation against `std::pmr::monotonic_buffer_resource` layered over a checking upstream that **aborts on any allocation** (proving zero leakage to `new_delete_resource`), plus `pmr::polymorphic_allocator` propagation assertions.
- **Gate**: `make test` green; the null-upstream test is the acceptance proof; header passes `make compile-headers`.

### WP-8 — Benchmarks, paper, docs (after WP-7; benchmark and prose halves may be two agents)

- **Files**: `benchmarks/beman/tree_algorithms/` (extend `build.bench.cpp`/`fold.bench.cpp`), `papers/algorithms-for-trees.md`, `README.md`, `docs/DECISIONS.md` (the WP-0 entry, finalized), `docs/notes/` cleanup.
- **Scope**: benchmarks in the Bench config per repo convention: default allocator vs monotonic vs `unsynchronized_pool_resource`, against the existing naive-tree baselines, for build, fold, and teardown; paper section presenting the allocator model (D-A1/D-A2 rationale, per-edge pointer cost stated candidly — costs stated by authors cannot be discovered by reviewers), with UUID anchors transcluding the new code; README touch-up.
- **Gate**: benchmarks run via the Makefile recipe and numbers are recorded in the handoff note; prose follows one-sentence-per-line; paper section reviewed by the managing agent against the voice of the existing sections.

## 5. Dependency graph and dispatch order

```
WP-0 ──► WP-1 ──► WP-2 ──┬──► WP-3 ──┐
                         ├──► WP-4 ──┤
                         ├──► WP-5 ──┼──► WP-7 ──► WP-8
                         └──► WP-6 ──┘
```

WP-0, WP-1, WP-2 are strictly serial — they define the vocabulary everyone else uses.
WP-3 through WP-6 fan out in parallel once WP-2 lands; they touch disjoint files except for shared read-only dependence on `box.hpp`/`child_slot.hpp`/`fix.hpp`.
If running WP-3…6 concurrently in worktrees, the managing agent merges in the order 3, 4, 5, 6 and re-runs `make test` after each merge.

## 6. Managing agent: supervision and quick-check protocol

For each WP, the managing agent:

1. **Dispatches** the WP brief: the WP section above, §2 verbatim, §3 verbatim, the §7 reporting instructions verbatim, and all handoff notes from completed prerequisite WPs.
2. **Quick-checks on completion** — cheap, mechanical, before reading the diff closely:
   - `make compile && make test` green; `make compile-headers` green when a header changed.
   - `git diff --stat` touches only the WP's listed files (plus module interface/CMake when declared); **nothing under `infra/`**.
   - New test files open with double-include + bootstrap test.
   - `grep` the diff for banned spellings: bare `Fix{`, `Box{`, `Identity{` (DEV-03), `#pragma once`, relative includes, `using namespace` in headers, `cata`/`ana`/`hylo` as identifiers.
   - Constexpr additions carry a `static_assert` test (DEV-04).
   - The handoff note exists and its deviations section is either empty or actioned (§7).
3. **Spot-verifies one claim per WP** by running it — e.g. for WP-1, actually run the tracking-allocator test target; for WP-7, run the null-upstream pmr test; for WP-8, re-run one benchmark and compare against the reported number.
4. **Forwards** each handoff note's summary and any deviations upward (to the user) at each merge point, and appends accepted deviations to the ledger (§7).
5. **Stops the line** if a deviation touches §3's decisions or `DECISIONS.md`: no dependent WP dispatches until the ADR and this plan are amended.

## 7. Sub-agent reporting: handoffs and deviations

Every sub-agent writes, as its final act, `docs/notes/allocator/HANDOFF-WP<N>.md`:

```markdown
# WP-<N> handoff — <title>

## Landed
<files touched; one line each on what changed>

## Interface decided
<exact spellings downstream WPs must use: signatures, tag conventions, aliases>

## Verified
<commands run and their results: make test, specific test names, benchmark numbers>

## Deviations
<DEV-A<nn>: what the plan said, what was done instead, why, what it breaks or frees downstream.
 "None." if none.>

## Open questions / warnings for downstream
<anything a later WP must know; "None." if none>
```

Deviation rules:

- A deviation is any departure from §3's decisions, a WP's scope/files list, or a gate — including "the plan's assumption was wrong."
- Number them DEV-A01, DEV-A02, … sequentially across the whole campaign; the managing agent owns the sequence and maintains the roll-up ledger `docs/notes/allocator/DEVIATIONS.md` (same shape as the fixpoint repo's `ops/DEVIATIONS.md`).
- Deviate-then-report is acceptable for anything inside a single WP's files; anything crossing WP boundaries or touching §3/`DECISIONS.md` requires stopping and escalating instead.
- Handoff notes are the interface between agents: a sub-agent must read its prerequisites' notes before writing code, and must not contradict an "Interface decided" section without a recorded deviation.

## 8. Definition of done

- All WP gates passed; `make test` and `make compile-headers` green at head; module build green.
- Every pre-existing test passes unmodified (backward compatibility is proven, not asserted).
- The pmr null-upstream test proves zero default-resource leakage for every representation.
- Benchmarks show the monotonic/pool numbers, whatever they are — the gate is honest measurement, not a favorable result.
- `DECISIONS.md` carries the allocator-model decision; the paper carries the section; the deviations ledger is complete.
