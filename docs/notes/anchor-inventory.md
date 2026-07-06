# Anchor inventory — transclusion contract (Tasks 3a/3b → 4.4)

Complete UUID-anchor map for docs/recursive-tree-algorithms.org and papers/DnnnnR0.
"Serves" line numbers refer to the article's TRANSCLUDE-TODO markers.

## Examples (Task 3b)

| Anchor | File | Region | Serves |
|---|---|---|---|
| ec8e7f28-10c3-43ea-bfa9-a54f4c63ef9c | examples/fixpoint_tree_example.cpp | compact Box + make_box | article L34 |
| 3959d7ab-f1fa-4fd7-b2b9-886a1f7610a2 | examples/fixpoint_tree_example.cpp | expression type system | article L69 |
| 02640f07-f080-4eec-bd90-b414fabd4b7b | examples/fixpoint_tree_example.cpp | smart constructors | article L91 |
| 2526ec45-2e52-4b56-aab0-1034b078a459 | examples/fixpoint_tree_example.cpp | main + one-layer eval + local fold | article L110 (region includes working local fold; trim decision at wiring time) |
| a66c0122-f24d-4858-a802-cf76f87969e9 | examples/expression_algorithms.cpp | example-local Foldable/Traversable maps | follow-up article (spare) |
| 868a8228-44a7-4761-8822-28b622d6b9a0 | examples/expression_algorithms.cpp | validate_impl | algorithm-object story |
| 7226de56-d7ce-44c6-9e99-849da548d3e4 | examples/expression_algorithms.cpp | transform_if_large_impl | algorithm-object story |
| fba6bbba-6cd8-4f4f-ba18-b584cf672a79 | examples/binary_tree_adapt.cpp | to_fix + shape algebra + fold + from_fix | article TODO(3a) L714 |

## Library headers and tests (Task 3a)

| Anchor | File | Region | Serves |
|---|---|---|---|
| a1f81cb9-ebb3-4872-9549-03ca140c61b2 | include/.../box.hpp | Box class | article L185 (first half) |
| 45a64b08-8217-4baf-b19b-146434de1e44 | include/.../box.hpp | make_box | article L185 (second half; concatenate both anchors) |
| 57d4bd6e-c8c7-4806-afd3-2e42aec8ae27 | include/.../fix.hpp | Fix struct | article L255 |
| 53775b7e-8a78-4b79-885b-046f6232d7a3 | include/.../fix.hpp | wrap_fix + unwrap_fix | article L266 |
| 085bb189-a48e-4262-aefd-b64f8755e959 | include/.../expression.hpp | ExprF base functor | article L150 |
| a052ddcb-f05b-41c9-85bf-4c443ab438b7 | include/.../expression.hpp | functor Impl/Map + specialization | article L328 |
| d0f9b5a2-363f-49ca-9a37-bfb0e6c9ff50 | include/.../expression.hpp | smart constructors | article L380 |
| bbf73c10-b63e-434a-91f1-d1a3a7c23b96 | include/.../expression.hpp | eval_algebra | article L422 |
| 66235297-8e2a-4610-b6a4-a3f2a8837fb0 | include/.../expression.hpp | eval function | article L543 |
| 7970dd71-b5dd-4fa0-8e80-735a26833b65 | include/.../functors.hpp | NatF family | article L625 |
| edbdeea5-9b3e-4d9a-88d5-fb2610c9174d | include/.../functors.hpp | NatF functor instance | spare |
| f419286e-ec56-4291-9f09-d5528685805f | include/.../functors.hpp | make_zero/make_succ/nat_from_int/nat_to_int | article L638 — RETARGET: article says recursion_schemes.test.cpp; coalgebra lives inline in nat_from_int |
| f6f4cee4 | include/.../binary_tree.hpp | BinaryTree class | article L714 section |
| 27aeff53 | include/.../binary_tree.hpp | BinaryTreeF + Layer/Fix aliases | article L714 section |
| 91fef612 | include/.../binary_tree.hpp | functor instance | article L714 section |
| a7ed3ac1 | include/.../binary_tree.hpp | to_fix/from_fix + trait | article L714 section |
| 5033568c-a02f-44b0-964b-7076ebb06d43 | tests/.../expression.test.cpp | build (1+2)*3 | article L403 |
| c1858641-8184-4907-ad06-14744a4ac1d2 | tests/.../expression.test.cpp | print_algebra fold | article L560 — RETARGET: article says recursion_schemes.test.cpp |

## Missing anchors (comment-only additions in include/, owned by Task 4.4)

- functor.hpp: `functor_typeclass` variable-template default → article L316
- recursion_schemes.hpp: `fold_fix` explicit-fmap overload → article L448
- recursion_schemes_lookup.hpp: `layer_fmap` → L511; `fold_fix` → L523; `unfold_fix` → L604; `refold` → L660
- papers/DnnnnR0 SYNOPSIS-TRANSCLUDE-TODO (~L165) needs the verb-signature anchors above.

## Snippet gate

`paper_snippets_check` custom target (top-level CMakeLists.txt, ALL) depends on the three anchored examples + tests.{box,fix,expression,functors,binary_tree}; negative-tested (injected syntax error fails the build).
