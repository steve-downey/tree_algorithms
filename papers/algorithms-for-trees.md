---
title: "Algorithms for Trees"
document: D4322R0
date: 2026-07-17
audience: LEWGI, LEWG
author:
  - name: Steve Downey
    email: <sdowney@gmail.com>
toc: true
toc-depth: 3
---

# Abstract

Recursive tree structures are common in parsing, transformation pipelines, symbolic representations, and hierarchical data processing, but the standard library offers little direct support for generic algorithms centered on recursive structure rather than on flat external iteration.

This paper proposes a focused family of recursive algorithms over recursive node structures.
The structural verbs `fold_fix` (recursive fold), `unfold_fix` (recursive build), and `refold` (fused build-then-fold) operate over fixed-point trees; `fold_with` and `unfold_with` are the same folds over a user's tree in its own representation, through an explicitly supplied projection or embedding, with no conversion and no fixed-point wrapper materialized; and `fold_map` is the elementwise fold — map each element, combine under an associative operation — derived from the structural verbs rather than specified as parallel machinery.
The unifying theme is that recursive trees are more naturally served by structure-directed traversal and reconstruction than by cursor-oriented generic interfaces alone.

Recursive trees expose generic algorithmic structure not well captured by sequence-centric iterator models, and C++ would benefit more from reusable recursive algorithms than from prematurely standardizing one blessed tree vocabulary type.
Accordingly, this is an algorithms paper.
The fixpoint machinery used in the reference implementation is evidence and implementation support, not the public identity of the proposal.

# Before / After

| Before                                                          | After                                          |
|-----------------------------------------------------------------|------------------------------------------------|
| Recursive structures handled through bespoke one-off algorithms | One reusable algorithm family over trees       |
| Iterator-centric interfaces that expose traversal mechanics     | Structure-directed traversal or reconstruction |
| Limited reuse across different recursive tree representations   | A generic story across more than one tree type |

Concretely, evaluating a variant-based expression tree today means hand-rolling the recursion at every use site:

::: cmptable

> Before

```cpp
// One-off recursive visitation:
// std::visit plus explicit recursion,
// rewritten for every algorithm.
int eval(const Expr& e) {
    return std::visit(overloaded{
        [](const Lit& l) {
            return l.value;
        },
        [](const Add& a) {
            return eval(*a.lhs)
                 + eval(*a.rhs);
        },
        [](const Mul& m) {
            return eval(*m.lhs)
                 * eval(*m.rhs);
        },
    }, e.node);
}
```

> After

```cpp
// The recursion is the algorithm's job;
// the user supplies only the per-layer
// algebra.
int result = fold_fix<int>(
    overloaded{
        [](const Lit<int>& l) {
            return l.value;
        },
        [](const Add<int>& a) {
            return a.lhs + a.rhs;
        },
        [](const Mul<int>& m) {
            return m.lhs * m.rhs;
        },
    },
    expr_fmap, tree);
```

:::

The "After" algebra is non-recursive.
Each alternative sees children that have already been reduced to the result type.
The same tree works with a pretty-printing algebra, a depth algebra, or a rewriting algebra without touching the traversal again, and the same `fold_fix` works over any recursive representation that supplies a mapping function for one structural layer.

# Motivation and scope

## Why recursive structure deserves direct treatment

The C++ standard library has strong support for sequence-oriented generic programming centered on external iteration.
That support remains valuable, but it is not the only credible foundation for generic algorithms.

For recursive structures, a different center of gravity is often better:

- internal traversal rather than external cursor choreography,
- structural reconstruction rather than elementwise mutation,
- a small user-supplied per-layer operation rather than a hand-maintained recursive function per algorithm.

Recursive structure creates algorithmic reuse opportunities that flat iterator-centric APIs do not capture well.
Flattening a tree into an iteration order discards exactly the structure that tree algorithms are about, and every project that manipulates expression trees, document trees, or hierarchical configuration reinvents the same recursive folds and rebuild passes locally.

## What is proposed — and what is deliberately not

The missing library surface is a family of verbs — reduce, fold, rebuild, build — not one blessed tree type.

This paper proposes one small family, in three groups:

- **Structural verbs over fixed-point trees**:
  `fold_fix` — recursive fold: consume a recursive structure bottom-up with a non-recursive per-layer algebra;
  `unfold_fix` — recursive build: grow a recursive structure top-down from a seed with a non-recursive per-layer coalgebra;
  `refold` — the fusion of the two: build-then-fold without materializing the intermediate structure.
- **The same verbs over trees in their own representation**:
  `fold_with` — fold through an explicitly supplied *projection* exposing one layer of the user's tree at a time;
  `unfold_with` — build through an explicitly supplied *embedding* reconstructing one layer of the user's representation.
  Both are `refold` in disguise; no conversion happens and no fixed-point wrapper is materialized.
- **The elementwise fold**:
  `fold_map` — map each element to the result type and combine under an explicitly supplied associative operation with identity; length, sums, renderings, and any/all queries are all `fold_map`s, and its implementation *is* the derivation from the structural verbs.

This paper explicitly does **not** propose a standard tree container or tree vocabulary type.
Prematurely standardizing one blessed tree layout would be both harder to agree on and less useful than the algorithms, because the algorithms work across representations users already have.
The motivating examples in the reference implementation span five recursive-tree representations, two of which are served with no fixed-point machinery at all (see [Implementation experience](#implementation-experience)).

This paper does not depend on the persistent-sequence container paper (Paper C in the coordinated set; see [Relationship to companion papers](#relationship-to-companion-papers)).
It consumes, but does not respecify, the bundled-customization mechanism of P3200 (Paper A) in an optional convenience layer.

# Design overview

## Fix and Box: exposition-level machinery

The reference implementation ties the recursive knot with a fixed-point wrapper.
A user-supplied *base functor* `F` describes one layer of structure with a type parameter in the recursive positions; `Fix<F>` closes the recursion:

```cpp
template <template <typename> class F>
struct Fix {
    F<Fix<F>> inner;   // composition, not inheritance
};
```

`Fix<F>` holds its layer as a data member, with `wrap_fix`/`unwrap_fix` as the explicit isomorphism.
The formulation `struct Fix : F<Fix<F>> {}` was considered and rejected: inheritance implies substitutability — any code expecting an `F<Fix<F>>` would accept a `Fix<F>` — which is not a property the design requires, and which can create ambiguity in overload resolution or template instantiation.
The has-a formulation says only what is true, and the wrapping is zero cost.

Recursive positions inside base-functor alternatives are held through `Box<A>`, a constexpr-capable, nullable, deep-copying owning pointer, so that `F<X>` is a complete type even for incomplete `X`.
`std::indirect` has the right semantics for this job, and it is the plausible future spelling; the reference implementation stays with a hand-rolled `Box` because `std::indirect`'s explicit default constructor blocks use in aggregate-initialized containers, and because depending on it would raise the implementation floor to C++26 library support.
In this paper both `Fix` and `Box` are exposition-level implementation detail: they are how the reference implementation demonstrates the algorithms, not names this paper asks LEWG to bless as user-facing vocabulary.

## Building on Fix directly

Exposition-level does not mean toy.
The subsections below treat the fixed-point wrapper as an adapter target for trees that already exist; it is equally a representation a user can build on from the start, and the reference implementation includes a full working container that does: a binary search tree whose tree type is `Fix` over a binary base functor directly, with a disengaged `Box` doing double duty as both "empty tree" and "absent child" (`examples/search_tree_on_fix.cpp`).

The example's value to this paper is its honest division of labor.
`unfold_fix` builds the tree balanced from a sorted range; `fold_fix` and `fold_map` implement the whole-tree consumers — flatten, height, shape.
But insert, contains, and erase are plain structural recursion written with `wrap_fix` and `unwrap_fix`, because search *prunes*: it descends one child and ignores the other, while a fold visits everything.
The fixed-point machinery does not take ordinary recursion away, and the proposed verbs do not pretend to subsume it.
(The literature's pruning and short-circuiting schemes — paramorphisms and apomorphisms — exist in the evidence catalog precisely for such operations; they remain deliberately unproposed. See [Non-goals](#non-goals).)
The whole lifecycle — insert, search, erase, flatten — is exercised at compile time in a `static_assert`.

## Explicit `fmap`: the primary API

The primary verbs take the base functor's mapping function — `fmap`, the function that applies a callable to each child position of one structural layer — explicitly as a callable parameter.

This has three consequences:

- The primary API has **no customization-mechanism dependency at all**.
  A user with any variant-based recursive type can call the verbs today by writing one per-layer mapping function.
- Result carriers are leading explicit template parameters (`fold_fix<Result>(...)`), because C++ cannot infer fold carriers through recursive calls the way languages with global type inference can.
- The verbs are ordinary function templates, `constexpr` throughout.

`fold_map` follows the same rule at the element level: its primary spelling takes the combine operation and its identity as plain parameters — two callables/values, no Monoid trait anywhere in the primary API.

## Trees you don't own: project and embed

Adapting an existing tree by conversion — building a fixed-point copy, folding it, converting back — is honest about its cost, but "convert your tree to our form first" is a weak answer for a paper whose thesis is that the algorithms should serve representations users already have.

The direct verbs close that gap with one more explicit callable each.
A *projection* exposes one layer of the user's tree as a base-functor value, `project : Tree -> F<Handle>`, where the child handles are whatever the projection itself accepts — raw pointers into the existing structure work, so nothing is copied beyond one layer at a time.
`fold_with(algebra, fmap_fn, project, tree)` is then exactly `refold` with the projection as its coalgebra; dually, `unfold_with(coalgebra, fmap_fn, embed, seed)` is `refold` with an *embedding* `embed : F<Tree> -> Tree` as its algebra.
No `Fix` is materialized in either direction, and `fold_fix` itself is the degenerate case whose projection is `unwrap_fix` — an equivalence the test suite pins at compile time.

Because the ingredients are three small callables — a layer struct, an `fmap`, a projection — the verbs reach trees that no conversion pipeline could serve: the reference implementation's example folds and *builds* a tree whose children are held by `std::unique_ptr`, a move-only structure that cannot be copied into any other form.
The projection and embedding remain plain callables in this revision; no `Recursive`/`Corecursive`-style concept is proposed for them (see [Non-goals](#non-goals)).

## Typeclass lookup: a separate convenience layer

Alongside the explicit-`fmap` verbs, lookup-based overloads drop the structural parameters and resolve them through typeclass-object lookup in the style proposed by P3200: `fmap` through `functor_typeclass<Layer>`, the elementwise layer fold through `layer_fold_typeclass<Layer>`, and the projection through `project_typeclass<Tree>`.
The everyday spelling of the elementwise fold is then four arguments — `fold_map<Result>(map_fn, combine, identity, tree)` — over either a fixed-point tree or a user's tree in its own representation.

These overloads live in separate convenience headers, and those headers are explicitly *consumers* of the P3200 bundled-customization facility.
This paper consumes the lookup mechanism; it never respecifies it.
The explicit primary verbs are the surface that remains fully valid even if P3200 does not progress at the same pace.

# Proposed API synopsis

Namespace and header names below follow the reference implementation (`beman::tree_algorithms`) and are placeholders for LEWG naming review.

<!--
This synopsis is hand-maintained, not transcluded: the WG21 pandoc pipeline
in fixpoint/main/papers/wg21 (data/filters/wg21.py, driven by the Makefile's
`$(PANDOC) --bibliography ...` rule) is a formatting filter only — it
resolves cmptable layout, stable names, and coloring, with no file-include
or transclusion directive comparable to org-transclusion. Markdown has no
equivalent mechanism available in this build. The signatures below mirror,
verbatim, the following anchored regions in beman.tree_algorithms:
  - include/beman/tree_algorithms/recursion_schemes.hpp
    fold_fix (explicit-fmap primary verb): anchor 6859fdfc-3e1c-4d8a-9375-521df52ff499
    (unfold_fix/refold explicit-fmap overloads follow immediately in the
    same header; they are not individually anchored)
  - include/beman/tree_algorithms/recursion_schemes_lookup.hpp
    fold_fix (lookup overload): anchor d4f69d2f-787b-4741-ae82-971f0ff36b9e
    unfold_fix (lookup overload): anchor 2df7f7e0-ee8b-42b6-a0a8-1dfed05c8230
    refold (lookup overload): anchor c75161e3-1aad-4549-86cc-1dc40d6623c8
  - include/beman/tree_algorithms/recursion_schemes.hpp
    fold_with (direct fold): anchor 6c596985-e990-4808-a504-2b651c34cebc
    unfold_with (direct build): anchor d160af7b-9a3c-4593-b8f5-fc1ff0404b37
  - include/beman/tree_algorithms/fold_map.hpp
    fold_map over Fix (explicit): anchor 52588460-d05a-43f9-97cc-01aa06732730
    fold_map direct (explicit): anchor 5bf78776-81e7-4b66-8487-6615adc33b17
  - include/beman/tree_algorithms/fold_map_lookup.hpp
    lookup variables: anchor 94d2dd48-8976-43fc-943d-0dd27177e41a
    fold_map over Fix (lookup): anchor e4beac7f-257b-4f2d-bfff-6b3b5daf61ad
    fold_map direct (lookup): anchor 7f08b12d-3d26-4b5e-bf79-66d38f6fee0d
Checked against: manually diffed against the headers above on 2026-07-06
(three verbs) and 2026-07-14 (fold family additions); identical modulo
trailing semicolons (this is a declaration-only synopsis) and line
wrapping. Carried into D4322R0 on 2026-07-17 with no header signature
changes since the 2026-07-14 diff. Re-diff before every revision that
touches these headers, since drift here will not be caught by the build.
-->
```cpp
namespace beman::tree_algorithms {

// Recursive fold (catamorphism in the literature).
// algebra : F<Result> -> Result
// fmap_fn : (recurse, layer) -> mapped-layer
template <typename Result, template <typename> class F, typename Algebra,
          typename FMap>
constexpr auto fold_fix(const Algebra& algebra, const FMap& fmap_fn,
                        const Fix<F>& tree) -> Result;

// Recursive build (anamorphism in the literature).
// coalgebra : Seed -> F<Seed>
template <template <typename> class F, typename Coalgebra, typename FMap,
          typename Seed>
constexpr auto unfold_fix(const Coalgebra& coalgebra, const FMap& fmap_fn,
                          const Seed& seed) -> Fix<F>;

// Fused build-then-fold (hylomorphism in the literature).
// No intermediate Fix<F> is materialized.
template <typename Result, template <typename> class F, typename Algebra,
          typename Coalgebra, typename FMap, typename Seed>
constexpr auto refold(const Algebra& algebra, const Coalgebra& coalgebra,
                      const FMap& fmap_fn, const Seed& seed) -> Result;

// The direct verbs: the same folds over a tree in its own
// representation, through an explicitly supplied projection or
// embedding. Both are refold in disguise; no Fix is materialized.
// project : Tree -> F<Handle>   (one layer; handles as project accepts)
// embed   : F<Tree> -> Tree     (rebuild one layer)

template <typename Result, typename Algebra, typename FMap,
          typename Project, typename Tree>
constexpr auto fold_with(const Algebra& algebra, const FMap& fmap_fn,
                         const Project& project, const Tree& tree)
    -> Result;

template <typename Tree, typename Coalgebra, typename FMap,
          typename Embed, typename Seed>
constexpr auto unfold_with(const Coalgebra& coalgebra, const FMap& fmap_fn,
                           const Embed& embed, const Seed& seed) -> Tree;

// The elementwise fold (separate header): map each element, combine
// under an explicit associative operation with identity. The
// per-representation ingredient is layer_fold, which folds one layer's
// mapped elements with already-folded child results in the
// representation's contractual order.

template <typename Result, template <typename> class F, typename MapFn,
          typename Combine, typename LayerFold, typename FMap>
constexpr auto fold_map(const MapFn& map_fn, const Combine& combine,
                        const Result& identity, const LayerFold& layer_fold,
                        const FMap& fmap_fn, const Fix<F>& tree) -> Result;

template <typename Result, typename MapFn, typename Combine,
          typename LayerFold, typename FMap, typename Project,
          typename Tree>
constexpr auto fold_map(const MapFn& map_fn, const Combine& combine,
                        const Result& identity, const LayerFold& layer_fold,
                        const FMap& fmap_fn, const Project& project,
                        const Tree& tree) -> Result;

// Lookup-based convenience overloads (separate headers; consume the
// P3200 bundled-customization facility, resolving fmap through
// functor_typeclass<Layer>, the layer fold through
// layer_fold_typeclass<Layer>, and the projection through
// project_typeclass<Tree>).

template <typename Result, template <typename> class F, typename Algebra>
constexpr auto fold_fix(const Algebra& algebra, const Fix<F>& tree)
    -> Result;

template <template <typename> class F, typename Coalgebra, typename Seed>
constexpr auto unfold_fix(const Coalgebra& coalgebra, const Seed& seed)
    -> Fix<F>;

template <typename Result, template <typename> class F, typename Algebra,
          typename Coalgebra, typename Seed>
constexpr auto refold(const Algebra& algebra, const Coalgebra& coalgebra,
                      const Seed& seed) -> Result;

template <typename Result, template <typename> class F, typename MapFn,
          typename Combine>
constexpr auto fold_map(const MapFn& map_fn, const Combine& combine,
                        const Result& identity, const Fix<F>& tree)
    -> Result;

template <typename Result, typename MapFn, typename Combine, typename Tree>
constexpr auto fold_map(const MapFn& map_fn, const Combine& combine,
                        const Result& identity, const Tree& tree) -> Result;

} // namespace beman::tree_algorithms
```

# Folding and traversing trees

## Elementwise folds: fold_map

The structural verbs consume a whole layer at a time; many everyday
queries see only elements.
Length, sums, to-string renderings, any/all queries — each maps every
element to a result type and combines the results under an associative
operation with an identity.
`fold_map` is that verb.

The obvious question is why `fold_map` when `std::ranges::fold_left`
exists.
The answer is this paper's whole subject: `fold_left` folds ranges, and
recursive trees are not ranges.
Flattening a tree into an iteration order to fold it discards the
structure, costs an intermediate sequence, and forecloses folds that
exploit the shape.
`fold_map` folds the tree where it stands — and because the combine
operation is required to be associative with a two-sided identity, the
result is independent of how the structure happens to be balanced.
The reference implementation demonstrates exactly that: the same element
sequence held in two differently shaped trees folds to the same result,
while a reordered sequence folds to a different one.

The per-representation ingredient is `layer_fold`: the callable that
folds one layer, combining that layer's mapped elements with the
already-folded child results in the representation's contractual
traversal order.
Traversal order is part of each instance's contract — in-order for the
value-at-every-node binary tree, sequence order for the fringe tree,
pre-order for the rose tree — and the test policy pins it with
non-commutative combines (string concatenation), under which a wrong
order is a wrong answer.

Order belongs to the instance because no generic order exists to impose.
A rose tree — a value and any number of children — has no "between the
children" position, so in-order traversal does not exist for that shape;
in-order is the binary layer's contract, not a property of trees.
The reference implementation pins the consequence in a test: the same
`fold_map` call over the same three values yields `"123"` through the
binary layer and `"213"` through the rose layer, and neither is wrong,
because there is no generic in-order to be right about.
Pre-order and post-order do generalize — every layer has a
before-children and an after-children moment — so a future generic
traversal surface could offer those.
Never in-order.

The derivation is the implementation, and that is the point: `fold_map`
over a fixed-point tree *is* `fold_fix` with an algebra that combines
one layer through the supplied operations, and `fold_map` over a
projected tree *is* `fold_with` with the same algebra.
Elementwise folding is derived from the structural verbs, not parallel
machinery — evidence that the small proposed core is the right primitive
layer.
The derivations continue past this paper's surface: the classic
Foldable operations (`fold_left` and its family) derive from `fold_map`
by folding elements into accumulator-transforming programs, exactly as
Haskell's `Data.Foldable` derives `foldl` from `foldMap`; the reference
implementation demonstrates the derivation, and the coordinated set
keeps those derived operations with the Foldable surface rather than
respecifying them here.

What counts as an element is the instance's choice, not the framework's.
The expression tree's elements are its constants: summing the constants
of `(1 + 2) * 3` by `fold_map` gives `6`, while evaluating it by
`fold_fix` gives `9`.
One fixture, both verbs, and the elementwise/structural distinction is
visible in the values.

## Traversal with effects: the P3200 boundary

A companion facility, P3200's `traverse`/`transpose`, covers the
remaining member of this family: rebuilding the *same shape* while
per-element effects accumulate in a context — validating every constant
in an expression tree into `optional<Expr>`, where one absent value
poisons the whole result but a present result preserves the exact
structure.
Where `fold_map` collapses a tree to a summary, `traverse` maps a tree
to a tree inside a context; `transpose` is `traverse` with the identity
function.

That facility is P3200's public identity, and this paper deliberately
does not respecify any of it.
Trees are one instance of P3200's "structure" axis, exactly as
`optional`, senders, and SIMD lanes are instances of its "context" axis;
the tree instances demonstrated in the reference repositories are
examples, not wording.
Like `fold_map`, `traverse` for any fixed-point tree is derivable
through the layer's mapping function — one more derived operation over
the same primitive layer, on the other side of the papers' division of
labor.

# Naming

The public identifiers are `fold_fix`, `unfold_fix`, `refold`, `fold_with`, `unfold_with`, and `fold_map`.
"Recursive fold" and "recursive build" are prose descriptions used in this paper; they are not identifiers.

The traditional names for these three schemes — catamorphism, anamorphism, hylomorphism — are precise, but they are jargon that most C++ programmers have no reason to know.
The Haskell `data-fix` library (0.3.4) reached the same conclusion and deprecated `cata`, `ana`, and `hylo` in favor of descriptive names; this proposal follows that lead.

| Traditional | This proposal | `data-fix`  |
|-------------|---------------|-------------|
| cata        | `fold_fix`    | `foldFix`   |
| ana         | `unfold_fix`  | `unfoldFix` |
| hylo        | `refold`      | `refold`    |

Alternatives considered and rejected:

| Candidate                          | Why rejected                                                                                                   |
|------------------------------------|----------------------------------------------------------------------------------------------------------------|
| `cata` / `ana` / `hylo`            | Jargon with no descriptive content for a C++ audience; `data-fix` precedent is to deprecate these in favor of descriptive names; not proposed even as aliases. |
| `catamorphism` / `anamorphism` / `hylomorphism` | Same jargon problem, longer.                                                                     |
| `recursive_fold` / `recursive_build` | Verbose as identifiers, and collides with the prose descriptions this paper uses to *explain* the verbs; the `_fix` suffix instead names the fixed-point mechanism the verbs operate through, matching `data-fix`. |
| `fold` / `unfold` alone            | Overclaims generality and collides with existing fold vocabulary (`std::ranges::fold_left` and friends) that is sequence-shaped, not structure-shaped. |

The reference evidence repository carries a `[[deprecated]] cata` alias for its own history; this proposal starts clean and does not include it.

The direct verbs are `fold_with` and `unfold_with`: fold and build *with* an explicitly supplied ingredient — the projection or embedding one would otherwise have to bake into a conversion.
The suffix reads consistently across the coordinated set, where `_with` marks "the thing you would otherwise look up, passed inline" (P3200 spells explicit-instance overloads `traverse_with`/`transpose_with` in the same spirit).

| Candidate                      | Why rejected                                                                                                            |
|--------------------------------|-------------------------------------------------------------------------------------------------------------------------|
| `fold_tree` / `unfold_tree`    | Names the subject rather than the distinguishing ingredient; the verbs are not tree-specific, only recursive-structure-specific. |
| `fold_from` / `unfold_into`    | Directional pair reads well for build, poorly for fold; asymmetric.                                                     |
| `recursive_fold` / `recursive_build` | Rejected for the structural verbs above; the same reasoning applies.                                              |
| `*_fix` spellings              | The `_fix` suffix names the fixed-point mechanism; the direct verbs pointedly never touch `Fix`, so borrowing it would misstate the design. |

`fold_map` keeps the cross-language consensus name (`foldMap` in Haskell's `Data.Foldable`, Scala's Cats, and PureScript): it says exactly what the verb does — map, then fold — and its contrast with `fold_fix` (elements versus layers) is the teaching device of the section that introduces it.

# Implementation experience

Two implementations back this paper.

**`beman.tree_algorithms`** is the standardization-facing implementation: exactly the surface proposed here — `Fix`/`Box`, the six verbs in both their explicit and lookup tiers, the three consumer-stub lookup objects, and adapters demonstrating the verbs over five recursive representations:

- the fixed-point expression tree (`Fix` over a variant base functor), plus a full working binary search tree built on the same machinery (see [Building on Fix directly](#building-on-fix-directly));
- a persistent value-at-every-node binary tree with `shared_ptr` children, served both by conversion (`to_fix`/`from_fix`) and directly (a projection reading the `shared_ptr` spine in place), with a test pinning that both routes fold the same tree to the same answer;
- a rose (multiway) tree, `Fix`-native: one base functor holding a value and a `std::vector` of children — no `Box` at all, because `vector` supports incomplete element types and already owns through indirection — whose layer fold contracts pre-order, the shape for which in-order does not exist;
- a fringe tree — values at leaves, branches carrying a cached measure (leaf count), O(1) concatenation by structural sharing — served *directly only*: its layer holds children by value, no `Fix` form exists for it, and its embedding routes through the measure-computing constructor, so every tree `unfold_with` builds carries correct cached measures by construction;
- a nonce tree with `std::unique_ptr` children — move-only, uncopyable, folded and built in place through three small ingredients.

It targets C++23, with tests in Catch2, including compile-time (`static_assert`) coverage for every verb, and equivalence laws pinning each derived form to the primitives: `fold_with` with `unwrap_fix` as its projection is `fold_fix`; `unfold_with` with `wrap_fix` as its embedding is `unfold_fix`; `fold_map` equals `fold_fix` with the monoid algebra written by hand.
Its test policy deliberately uses non-idempotent, order-sensitive algebras, because experience showed that idempotent or commutative-monoid-only test functions fail to distinguish a correct traversal from a plausibly wrong one.
The policy caught a real defect during this paper's own development: a draft `fold_map` test used `combine(a, b) = a*10 + b` as its "non-commutative combine" — an operation that is neither associative nor equipped with a two-sided identity, and the leaf positions, which combine against the identity, exposed it immediately in a failing `static_assert`.
The canonical lawful non-commutative monoid — strings under concatenation — is what the suite uses, at compile time as well as runtime.

**The `smd::fixpoint` evidence repository** implements, on top of the same three-verb core, the full catalog of recursion schemes from the literature: para, apo, zygo, mutu, prepro, postpro, histo, futu, dyna, codyna, chrono, Mendler-style mcata/mhisto, Elgot (co)algebras, distributive laws, and the generalized gcata/gana/ghylo family, each gated by equivalence-law tests pinning it to the core verbs (for example `gcata(dist_cata) ≡ fold_fix`).
That catalog is deliberately **not** proposed here.
Its role is evidence: the three proposed verbs are the stable primitive layer of a much larger, independently well-studied algorithm family, and nothing in the extended catalog required revisiting their design.

## Costs

We are upfront about the costs of this design; they are better stated by the authors than discovered by reviewers.
The library chooses value semantics throughout — layers are passed by value, `Box` deep-copies, and there is no sharing or memoization — and the abstraction itself has two costs beyond the copies.
First, compile time: C++ lacks higher-kinded types, so the `fmap` that `fold_fix` requires is emulated by template machinery, and the compiler performs deep, recursive template instantiation to type-check each scheme at each carrier type.
Across a large codebase this instantiation overhead can measurably degrade build latency.
<!-- MEASURE: no compile-time numbers exist for beman.tree_algorithms; the claim is qualitative, from the source cost analysis. Re-measure (e.g. -ftime-trace) before publication. -->

Second, code generation: a hand-written `std::visit` traversal compiles to a jump table or switch with O(1) dispatch, whereas a recursion scheme relies entirely on the optimizer to inline the generic traversal, the algebra, and the `Box` unwrapping into an equivalent loop.
When the structure's nesting exceeds inlining thresholds, the result is larger object code and runtime pointer-chasing rather than a fused traversal.
<!-- MEASURE: codegen size and per-node cost versus a manual std::visit walker have not been re-measured for this implementation. -->
These costs amortize well in one common deployment: the traversal machinery is stable and lives in rarely-recompiled headers, while the frequently-edited code consists of small, non-recursive algebras that compile like ordinary function objects.

We also note the language-evolution horizon.
Core-language pattern matching (P1371 "Pattern Matching"; superseded by P2688 "Pattern Matching: match Expression", still under EWG review after missing C++26) would replace the per-node dispatch inside an algebra, not the recursive traversal itself.
The proposed facility degrades gracefully into that future: algebras get shorter under a `match` expression, and `fold_fix` continues to supply the fold that the core language will still not provide.

# Allocator awareness

The trees this paper's reference implementation builds own their elements, have value semantics, and allocate — they are very nearly containers, and the container contract's remaining piece is allocator awareness under the uses-allocator protocol.
This section presents the model, defends the one place it departs from the usual convention, and reports what it costs, measured.

<!--
This section's code excerpts mirror, verbatim except for trimming to the
relevant lines, the following anchored regions in beman.tree_algorithms
(added across the allocator-awareness work; see
docs/notes/allocator-awareness-plan.md and docs/DECISIONS.md Decision 9
for the full design record this section summarizes):
  - include/beman/tree_algorithms/box.hpp
    Box class (allocator members): anchor a1f81cb9-ebb3-4872-9549-03ca140c61b2
  - include/beman/tree_algorithms/expression.hpp
    Add/Mul/ExprLayer (Box-at-knot, allocator-parameterized):
      anchor 085bb189-a48e-4262-aefd-b64f8755e959
    ExprFFmapAlloc/expr_fmap (allocator-carrying fmap):
      anchor a052ddcb-f05b-41c9-85bf-4c443ab438b7
  - include/beman/tree_algorithms/binary_tree.hpp
    BinaryTree::node/make_ptr (allocate_shared spine):
      anchor f6f4cee4-9c55-4add-b38b-496936319294
  - include/beman/tree_algorithms/pmr.hpp
    allocator_type/Box alias/make_slot: anchor d91724ee-59a9-4e14-8681-2a140cef0266
Checked against: manually diffed against the headers above on 2026-07-18.
Re-diff before every revision that touches these headers, since drift here
will not be caught by the build.
-->

## Edges carry the allocator

`Fix<F>` is a bare recursive value, not a container, so nothing in its own type has anywhere to keep an allocator.
The owning edges keep it instead: `Box<A, Allocator = std::allocator<A>>` stores its allocator through `[[no_unique_address]]`, which costs nothing for the default `std::allocator` — `sizeof(Box<int>) == sizeof(int*)` is enforced by a `static_assert`.

```cpp
template <typename A, typename Allocator = std::allocator<A>>
struct Box {
    using allocator_type = Allocator;
    [[no_unique_address]] Allocator alloc{};
    A*                              ptr = nullptr;
    // ...
};
```

An allocator supplied at a construction site flows to children by uses-allocator construction — `std::uninitialized_construct_using_allocator` inside `Box`, `std::make_obj_using_allocator` at the inline `child_slot` position — exactly as a `std::pmr` container propagates its resource to elements that accept one.
State-carrying edges honor the ordinary container rules: `select_on_container_copy_construction` on copy, the allocator stolen on move, POCCA/POCMA consulted on assignment with an element-wise fallback when the traits refuse propagation and the allocators compare unequal, POCS on swap.

## Layer types stay aggregates — a departure defended

Every operational class getting `allocator_type`, `get_allocator()`, and an allocator-extended constructor is the familiar convention; every standard container follows it.
This library's layer types — `BinaryTreeF`, `ExprF`, `RoseF`, and their siblings — do not.
They stay plain aggregates, and braced-init of a layer stays exactly the reading-and-writing exercise it always was, with no allocator-extended constructor to route around.
Allocators enter a tree only at the factory and verb surface — `make_box`, `make_slot`, `rose`, the smart constructors, and the allocator-carrying `fmap` described below — never through a layer's own constructor.
`std::uses_allocator` is, as a direct consequence, not specialized for `Fix` or for any layer type; the uses-allocator protocol members live only where an allocator is actually stored, which is `Box` and the `shared_ptr`-based representations described below.

This is a deliberate departure from "every class in an allocator-aware library gets `allocator_type`," and it earns a defense rather than a shrug.
The convention exists for container types, whose contract includes remembering how to allocate more storage after construction — a `vector` grows; an allocator-aware layer never does.
A layer here is a value produced once, by one construction, and never mutated in place: it either has an allocator-storing edge inside it already (a `Box`, a `shared_ptr`), or it is a leaf with no recursive position and nothing to allocate at all.
Giving every layer type its own `allocator_type` and constructor overload would change nothing about what gets allocated or how; it would only ripple an allocator-extended constructor into every aggregate literal in the codebase and this paper, in exchange for a protocol member that would always just forward to the one edge that actually stores anything.

## The cost, stated plainly

A stateful allocator — `std::pmr::polymorphic_allocator`, specifically — costs one pointer per `Box`, which is one pointer per heap edge.
That is the same per-node overhead a pmr node-based standard container pays; nothing about the fixed-point representation avoids it or hides it.
We say so here rather than let a reviewer find it.
Because `[[no_unique_address]]` erases the default allocator entirely, the cost is opt-in: a tree built with `std::allocator` pays exactly what it paid before this work, and a tree that opts into a stateful allocator pays the pointer it would pay as a node in any other allocator-aware container.

## Box-at-knot layers: the Option A refinement

Not every layer can hold a stateful allocator in a defaulted `child_slot_t<A>` field, and the first version of this design ran into that wall directly.
A layer built around a knot `Box` — the expression tree's `Add`/`Mul`, the binary tree's internal node, the natural-number successor — declares its recursive child as `child_slot_t<A>`, and with the allocator parameter defaulted, that field's type is exactly `Box<Fix<F>, std::allocator<Fix<F>>>`: the allocator is baked into the layer's field type, and it is the stateless default.
A `pmr::polymorphic_allocator` produces a different `Box` type, and there is no construction site — not `fmap`, not `wrap_fix`, not a smart constructor — that can put a stateful allocator into a field whose type has already fixed the allocator to `std::allocator`.

The resolution parameterizes the Box-at-knot layers on the allocator directly, binding it before `Fix` with a small binder type that exposes the unary-in-`A` alias template the verbs require, the same shape as the rose tree's pre-existing `RoseLayer<T>` binder:

```cpp
template <typename A, typename Allocator = expr_default_allocator>
struct Add {
    child_slot_t<A, Allocator> left, right;
};

template <typename Allocator>
struct ExprLayer {
    template <typename A>
    using F = std::variant<Const<A>, Add<A, Allocator>, Mul<A, Allocator>>;
};
```

The familiar aliases (`Expr`, `ExprF`) become the default-allocator instantiation of that binder, so every existing spelling keeps compiling with an unchanged type.
A defaulted template parameter does not cost a layer its aggregate-ness, so this refines rather than reverses the aggregate decision above: `Add<int>{...}` still braced-initializes exactly as it always did, over one more template parameter than before.

The verbs themselves get no allocator overload at all — `unfold_fix`, `refold`, and `fold_fix` keep the signatures shown earlier in this paper.
A stateful allocator cannot live in a field whose type is fixed to `std::allocator`, so it cannot ride inside an ordinary `fmap` primitive either; the fix takes the same shape as the layer fix, binding the allocator before use.
An allocator-carrying `fmap` is a small callable that captures an allocator value and threads it into the tagged `make_slot` it uses to build boxes, so every node an unfold or a refold constructs allocates through the captured allocator, with no allocator parameter on the verb itself:

```cpp
template <typename Allocator>
struct ExprFFmapAlloc {
    Allocator alloc;

    template <typename Fn, typename A>
    constexpr auto operator()(Fn&& fn, const typename ExprLayer<Allocator>::template F<A>& layer) const {
        // ... visits the layer, boxing each mapped child through
        // make_slot(std::allocator_arg, alloc, ...) instead of the
        // default-allocator make_slot.
    }
};
```

This is consistent with the primary API's own rule: `fmap` is already the customization point the verbs take explicitly, and an allocator-carrying `fmap` is one more instance of it, not a new mechanism.
A verb-level allocator parameter remains meaningful only for the lookup-tier verbs, which choose their `fmap` by looking it up rather than receiving it, and can look up the allocator-carrying instance exactly as they look up any other.

## The shared_ptr spine

Two representations in this library hold children through `shared_ptr` rather than `Box` — the persistent value-at-every-node binary tree and the fringe tree — and `shared_ptr` already has its own allocator story, which this library reuses rather than reinvents.
`std::allocate_shared` combines the control block and the object into a single allocation, and the control block remembers the allocator that built it, so no per-node storage is added to either representation:

```cpp
template <typename Allocator>
static auto node(std::allocator_arg_t, const Allocator& alloc, T value, BinaryTree left, BinaryTree right)
    -> BinaryTree {
    return BinaryTree(std::move(value),
                      make_ptr(std::allocator_arg, alloc, std::move(left)),
                      make_ptr(std::allocator_arg, alloc, std::move(right)));
}

template <typename Allocator>
static auto make_ptr(std::allocator_arg_t, const Allocator& alloc, BinaryTree tree)
    -> std::shared_ptr<BinaryTree> {
    return std::allocate_shared<BinaryTree>(alloc, std::move(tree));
}
```

A mutating operation that allocated later would need its own stored allocator, but these trees are persistent — every operation rebuilds the path it touches and shares the rest — so nothing here ever allocates after construction, and the control block is the only place an allocator needs to live.

## The pmr surface

A `beman::tree_algorithms::pmr` namespace collects the ergonomics for `std::pmr::polymorphic_allocator`: one allocator alias, and a thin, allocator-bound factory for every representation, each one forwarding to the tag-first factory its own representation's header already provides.

```cpp
using allocator_type = std::pmr::polymorphic_allocator<std::byte>;

template <typename A>
using Box = beman::tree_algorithms::Box<
    A, typename std::allocator_traits<allocator_type>::template rebind_alloc<A>>;
```

The surface is runtime-only, and its header says so directly: `std::pmr::polymorphic_allocator` dispatches every allocation through a virtual `memory_resource*`, so nothing in it is `constexpr`, and its existence changes nothing about the constexpr contract the default-allocator paths already satisfy.
Every representation this paper's implementation experience section describes — the rose tree, the expression tree, the binary tree, and the fringe tree — has a `pmr` factory and a test proving it against a `monotonic_buffer_resource` whose upstream is `std::pmr::null_memory_resource()`, so no build, fold, or unfold of a `pmr` tree can silently fall back to the global default resource.

## Measurements

We measured build, fold, and teardown for the binary-tree representation against three allocator routes — default `std::allocator`, `std::pmr::monotonic_buffer_resource`, and `std::pmr::unsynchronized_pool_resource`, each stood up fresh per sample — alongside the existing hand-rolled `shared_ptr`/`unique_ptr` baselines.
The runs were single-sample-set and machine-dependent: several rows carry a standard deviation on the order of their own mean, and a second run on a quieter machine reproduced the *direction* of every finding below but not always its *magnitude*, sometimes by a factor of two or more.
We report both runs where the spread matters and treat every number here as directional, not as a precise, reproducible constant.

The robust claim is mechanistic, not a number: a monotonic resource's `deallocate` is a no-op, so the tree's destructor still walks and destroys every value but frees nothing per node, and the whole arena drops at once, later, outside the timed region.
Teardown over a 4,095-node tree is where that shows up most clearly, and it is also the finding most sensitive to which run you read:

| route | run 1 (loaded machine) | run 2 (quieter machine) |
|---|---|---|
| default `std::allocator` | 224.3 μs | 47.2 μs |
| monotonic | 28.5 μs | 17.9 μs |
| pool | 325.1 μs | 93.8 μs |

Monotonic teardown was **7.9× faster than default in run 1 and 2.6× faster in run 2** — a real and reproducible advantage in direction, but a magnitude that swings with the machine, driven mostly by volatility in the *default*-allocator row (224 μs → 47 μs) rather than in monotonic's own (28.5 μs → 17.9 μs, comparatively stable across both runs).
We state the range, roughly 2.6×–7.9× faster, rather than headline the larger single-run figure.
Build shows a smaller version of the same mechanism: over a 65,535-node tree, monotonic build ran roughly 2.3× faster than default (6.80 ms vs 15.59 ms, run 1), consistent with a monotonic resource turning many small allocations into a handful of geometrically-growing arena allocations; this number was not independently re-run and should be read with the same single-run caveat.

The pool resource did not win, on either build or teardown, in either run: `unsynchronized_pool_resource` was slower than default in both — roughly 1.2–1.45× slower in run 1, and 1.99× slower in run 2's teardown.
The likely explanation is methodological rather than a property of pool resources in general: every sample in this benchmark constructs a fresh, cold pool, populating empty per-size-class free lists from its upstream with no chance to amortize that cost across reuse, which is not the deployment a pool resource is designed for — building and tearing down many trees against one long-lived, already-warm pool.
We report the number as measured rather than explain it away, because the gate for this section is honest measurement, not a favorable result; a benchmark that reuses one pool across many build/teardown cycles is the natural follow-up if the pool story is to be settled either way.

The fold section is the noisiest in the run and does not cleanly show the flat line the model predicts: default, monotonic, and pool folds spread from 2.29 ms to 8.48 ms over the same tree, with every fold row's standard deviation on the same order as its mean — the signature of a measurement dominated by system noise rather than algorithmic cost, at a read that itself takes well under a millisecond at this tree size.
Folding is expected to be resource-independent, since a fold only reads and reading never touches an allocator, and the much larger, more controlled fold benchmark elsewhere in this repository is consistent with that expectation; this section's numbers should be retaken on an idle machine before anyone treats the spread as a real effect.

# Non-goals

- **Standardizing one canonical tree vocabulary type.**
  This is an algorithms proposal; no blessed tree layout or preferred recursive representation strategy is proposed.
- **A `Recursive`/`Corecursive` concept for projections and embeddings.**
  The direct verbs take `project` and `embed` as plain callables; no concept constrains them and no typeclass registers them beyond the optional lookup convenience.
  Plain callables compose unchanged with the generalized schemes of the evidence catalog; a concept can be layered on later without disturbing anything proposed here.
- **An elementwise traversal (`traverse`/`transpose`).**
  Shape-preserving effectful traversal is P3200's public identity; this paper's tree instances of it are examples, never wording (see [Folding and traversing trees](#folding-and-traversing-trees)).
- **The exotic recursion-scheme catalog.**
  para, apo, zygo, histo, futu, the distributive laws, and the generalized family remain implementation evidence, out of scope for this paper.
- **Performance-tuned traversal.**
  The proposed semantics are value semantics: layers are held and passed by value, copies are accepted, and there is no sharing, no move-optimization pass, and no memoization.
  Performance work is an explicit non-goal for this revision.
- **Stack-safety guarantees.**
  The algorithms are recursive; deep structures can overflow the stack, as the naive functional formulations can.
- **General graph algorithms.**
  Recursive trees only; graphs are a different proposal space.
- **Dependence on the persistent-sequence paper.**
  Nothing here requires prior acceptance of Paper C.

# Relationship to companion papers

This paper is part of a coordinated set, each paper independently reviewable.

**What this paper covers on its own**: the six verbs with their explicit primary APIs — explicit `fmap`, explicit projection/embedding, explicit combine and identity — usable with no other paper adopted.

**P3200 (Paper A: traversal, transposition, and bundled customization)**: proposes the bundled-customization mechanism (typeclass-object lookup) and the shape-preserving traversal/transposition story over familiar vocabulary types.
This paper *consumes* that mechanism in its convenience overloads — `fold_fix(algebra, tree)` resolving `fmap` through `functor_typeclass`, `fold_map(map_fn, combine, identity, tree)` resolving its structural ingredients the same way — and never respecifies it.
The mechanism debate belongs in P3200; the division of labor is deliberate.
The elementwise fold proposed here and the effectful traversal proposed there are two halves of one family, split on motivation: `fold_map`'s case is structures that are not ranges, which is this paper's subject; `traverse`'s case is contexts (`optional`, senders, SIMD lanes), which is P3200's.

**Paper C (persistent measured sequence)**: an independently motivated container proposal.
There is no normative dependence in either direction; persistent tree structures are simply one more recursive representation the algorithms here can serve.
The fringe tree in this paper's reference implementation — a cached measure maintained through its embedding — is the miniature of Paper C's monoid-tagged-structure idea, and the shared `Monoid` vocabulary (`identity`/`combine`) is kept aligned across the coordinated set.
The Foldable derivations sit on the same boundary: `fold_map` is the primitive this paper proposes, and the operations that derive from it (`fold_left` and its family) live with the Foldable surfaces in the companion repositories — one definition each, derived, never respecified here.

**Monad, deferred**: no paper in the coordinated set proposes a Monad abstraction — deferred, not rejected.
The typeclass design points toward a consistent generic name for sequential composition eventually (today's `and_then`/`transform`/`let_value` are per-type members, not generic), and the mechanism demonstrably carries it; it is simply not needed by anything proposed — the verbs here fold and build, and the traversal paper composes independent contexts.
The rationale is stated once, citably, in P3200's "Why not Monad"; this paper records only that the omission is a decision.

**What remains valid if companion papers do not progress**: the entire primary API.
Only the lookup-based convenience overloads are contingent on P3200's facility.

# References

- Steve Downey, `beman.tree_algorithms` reference implementation. <https://github.com/steve-downey/tree_algorithms>
- Steve Downey, `smd::fixpoint` evidence repository (full recursion-scheme catalog) and *Fixpoint Trees in C++*.
- P3200, the companion traversal/transposition and bundled-customization paper (Paper A of this coordinated set).
- Haskell `data-fix` 0.3.4, `Data.Fix`: `foldFix`/`unfoldFix`/`refold` naming precedent. <https://hackage.haskell.org/package/data-fix-0.3.4/docs/Data-Fix.html>
- Haskell `base`, `Data.Foldable`: the `foldMap` naming and derivation precedent for `fold_map`. <https://hackage.haskell.org/package/base/docs/Data-Foldable.html>
- Haskell `containers`, `Data.Tree`: the rose-tree precedent. <https://hackage.haskell.org/package/containers/docs/Data-Tree.html>
- Edward Kmett, `recursion-schemes` (Haskell package): the extended catalog implemented as evidence. <https://hackage.haskell.org/package/recursion-schemes>
- Erik Meijer, Maarten Fokkinga, Ross Paterson, *Functional Programming with Bananas, Lenses, Envelopes and Barbed Wire*, FPCA 1991.
- Steve Downey, *Concept Maps Using C++23 Library Tech*. <https://sdowney.org/posts/index.php/2024/05/19/concept-maps-using-c23-library-tech/>
- P2988, `std::optional<T&>`; and `std::indirect` (P3019), the plausible future spelling of the `Box` exposition detail.
