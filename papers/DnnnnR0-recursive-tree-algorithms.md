---
title: "Recursive Tree Algorithms"
document: DnnnnR0
date: 2026-07-05
audience: LEWGI, LEWG
author:
  - name: Steve Downey
    email: <sdowney@gmail.com>
toc: true
toc-depth: 3
---

# Abstract

Recursive tree structures are common in parsing, transformation pipelines, symbolic representations, and hierarchical data processing, but the standard library offers little direct support for generic algorithms centered on recursive structure rather than on flat external iteration.

This paper proposes a focused family of recursive algorithms over recursive, variant-based node structures: `fold_fix` (recursive fold), `unfold_fix` (recursive build), and `refold` (fused build-then-fold).
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

This paper proposes exactly three algorithms:

- `fold_fix` — recursive fold: consume a recursive structure bottom-up with a non-recursive per-layer algebra.
- `unfold_fix` — recursive build: grow a recursive structure top-down from a seed with a non-recursive per-layer coalgebra.
- `refold` — the fusion of the two: build-then-fold without materializing the intermediate structure.

This paper explicitly does **not** propose a standard tree container or tree vocabulary type.
Prematurely standardizing one blessed tree layout would be both harder to agree on and less useful than the algorithms, because the algorithms work across representations users already have.
The motivating examples in the reference implementations span more than one recursive-tree representation.

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

## Explicit `fmap`: the primary API

The primary verbs take the base functor's mapping function — `fmap`, the function that applies a callable to each child position of one structural layer — explicitly as a callable parameter.

This has three consequences:

- The primary API has **no customization-mechanism dependency at all**.
  A user with any variant-based recursive type can call the verbs today by writing one per-layer mapping function.
- Result carriers are leading explicit template parameters (`fold_fix<Result>(...)`), because C++ cannot infer fold carriers through recursive calls the way languages with global type inference can.
- The verbs are ordinary function templates, `constexpr` throughout.

## Typeclass lookup: a separate convenience layer

Alongside the explicit-`fmap` verbs, lookup-based overloads drop the `fmap_fn` parameter and resolve the mapping function through the functor typeclass-object lookup (`functor_typeclass<Layer>`) proposed by P3200.

These overloads live in a separate convenience header, and that header is explicitly a *consumer* of the P3200 bundled-customization facility.
This paper consumes the lookup mechanism; it never respecifies it.
The explicit-`fmap` primary verbs are the surface that remains fully valid even if P3200 does not progress at the same pace.

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
Checked against: manually diffed against the headers above on 2026-07-06;
identical modulo trailing semicolons (this is a declaration-only synopsis)
and line wrapping. Re-diff before every revision that touches these
headers, since drift here will not be caught by the build.
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

// Lookup-based convenience overloads (separate header; consumes the
// P3200 bundled-customization facility, resolving fmap through
// functor_typeclass<Layer>).

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

} // namespace beman::tree_algorithms
```

# Naming

The public identifiers are `fold_fix`, `unfold_fix`, and `refold`.
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

# Implementation experience

Two implementations back this paper.

**`beman.tree_algorithms`** is the standardization-facing implementation: exactly the minimal core proposed here — `Fix`/`Box`, the three verbs, functor-only typeclass lookup in the convenience layer, and tree adapters demonstrating the verbs over more than one recursive representation.
It targets C++23, with tests in Catch2, including compile-time (`static_assert`) coverage for every verb.
Its test policy deliberately uses non-idempotent, order-sensitive algebras, because experience showed that idempotent or commutative-monoid-only test functions fail to distinguish a correct traversal from a plausibly wrong one.

**The `smd::fixpoint` evidence repository** implements, on top of the same three-verb core, the full catalog of recursion schemes from the literature: para, apo, zygo, mutu, prepro, postpro, histo, futu, dyna, codyna, chrono, Mendler-style mcata/mhisto, Elgot (co)algebras, distributive laws, and the generalized gcata/gana/ghylo family, each gated by equivalence-law tests pinning it to the core verbs (for example `gcata(dist_cata) ≡ fold_fix`).
That catalog is deliberately **not** proposed here.
Its role is evidence: the three proposed verbs are the stable primitive layer of a much larger, independently well-studied algorithm family, and nothing in the extended catalog required revisiting their design.

## Costs

We are upfront about the costs of this design; they are better stated by the authors than discovered by reviewers.
The library chooses value semantics throughout — layers are passed by value, `Box` deep-copies, and there is no sharing or memoization — and the abstraction itself has two costs beyond the copies.
First, compile time: C++ lacks higher-kinded types, so the `fmap` that `fold_fix` requires is emulated by template machinery, and the compiler performs deep, recursive template instantiation to type-check each scheme at each carrier type.
Across a large codebase this instantiation overhead can measurably degrade build latency.
<!-- MEASURE: no compile-time numbers exist for beman.tree_algorithms; the claim is qualitative, from the source cost analysis. Re-measure (e.g. -ftime-trace) before P-numbering. -->

Second, code generation: a hand-written `std::visit` traversal compiles to a jump table or switch with O(1) dispatch, whereas a recursion scheme relies entirely on the optimizer to inline the generic traversal, the algebra, and the `Box` unwrapping into an equivalent loop.
When the structure's nesting exceeds inlining thresholds, the result is larger object code and runtime pointer-chasing rather than a fused traversal.
<!-- MEASURE: codegen size and per-node cost versus a manual std::visit walker have not been re-measured for this implementation. -->
These costs amortize well in one common deployment: the traversal machinery is stable and lives in rarely-recompiled headers, while the frequently-edited code consists of small, non-recursive algebras that compile like ordinary function objects.

We also note the language-evolution horizon.
Core-language pattern matching (P1371 "Pattern Matching"; superseded by P2688 "Pattern Matching: match Expression", still under EWG review after missing C++26) would replace the per-node dispatch inside an algebra, not the recursive traversal itself.
The proposed facility degrades gracefully into that future: algebras get shorter under a `match` expression, and `fold_fix` continues to supply the fold that the core language will still not provide.

# Non-goals

- **Standardizing one canonical tree vocabulary type.**
  This is an algorithms proposal; no blessed tree layout or preferred recursive representation strategy is proposed.
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

**What this paper covers on its own**: the three recursive algorithms with the explicit-`fmap` primary API, usable with no other paper adopted.

**P3200 (Paper A: traversal, transposition, and bundled customization)**: proposes the bundled-customization mechanism (typeclass-object lookup) and the shape-preserving traversal/transposition story over familiar vocabulary types.
This paper *consumes* that mechanism in its convenience overloads — `fold_fix(algebra, tree)` resolving `fmap` through `functor_typeclass` — and never respecifies it.
The mechanism debate belongs in P3200; the division of labor is deliberate.

**Paper C (persistent measured sequence)**: an independently motivated container proposal.
There is no normative dependence in either direction; persistent tree structures are simply one more recursive representation the algorithms here can serve.

**What remains valid if companion papers do not progress**: the entire primary API.
Only the lookup-based convenience overloads are contingent on P3200's facility.

# References

- Steve Downey, `beman.tree_algorithms` reference implementation. <https://github.com/steve-downey/tree_algorithms>
- Steve Downey, `smd::fixpoint` evidence repository (full recursion-scheme catalog) and *Fixpoint Trees in C++*.
- P3200, the companion traversal/transposition and bundled-customization paper (Paper A of this coordinated set).
- Haskell `data-fix` 0.3.4, `Data.Fix`: `foldFix`/`unfoldFix`/`refold` naming precedent. <https://hackage.haskell.org/package/data-fix-0.3.4/docs/Data-Fix.html>
- Edward Kmett, `recursion-schemes` (Haskell package): the extended catalog implemented as evidence. <https://hackage.haskell.org/package/recursion-schemes>
- Erik Meijer, Maarten Fokkinga, Ross Paterson, *Functional Programming with Bananas, Lenses, Envelopes and Barbed Wire*, FPCA 1991.
- Steve Downey, *Concept Maps Using C++23 Library Tech*. <https://sdowney.org/posts/index.php/2024/05/19/concept-maps-using-c23-library-tech/>
- P2988, `std::optional<T&>`; and `std::indirect` (P3019), the plausible future spelling of the `Box` exposition detail.
