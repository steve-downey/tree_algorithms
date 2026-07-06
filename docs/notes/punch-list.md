# Punch list — running items for the release sweep (plan Task 5.1)

Accumulated from sub-agent reports; resolve or explicitly waive before R0.

## Blocking / needs Steve

- GitHub repo not yet created: `gh repo create steve-downey/tree_algorithms --public` was blocked by the session permission classifier; create it and push, then verify CI.
- Request a real WG21 paper number to replace the `DnnnnR0` placeholder.
- Decide whether to upstream copier-template fixes to `steve-downey/exemplar` (items below marked *template*).

## From Task 1.2 (CI matrix trim)

- README badges (lines ~8-12, 19, 87) point at `github.com/bemanproject/tree_algorithms`; must become `steve-downey/tree_algorithms`. Coveralls badge likewise. *template?*
- `vcpkg-configuration.json` references `bemanproject/vcpkg-registry` port `beman-tree-algorithms`, which does not exist; the `vcpkg-ci` job will likely fail on the fork. Disable or point elsewhere.
- Issue/PR templates link to `bemanproject/beman` docs — cosmetic.
- `cmake/llvm-toolchain.cmake` and `cmake/ci-clang-toolchain.cmake` hard-code `-std=c++20`, below the library floor; bump to c++23. *template*
- CONFIRMED BLOCKING (Task 3c, partial run): `cmake/clang-flags.cmake` pins `-std=gnu++20`, so `make TOOLCHAIN=clang-22 test` fails on deducing-this in functor.hpp. Bump to gnu++23/c++23 before any clang toolchain build. *template*
- README line ~39 says "(Test Only) GoogleTest" — stale for a catch2 project; fix text (and check the template's catch2 branch). *template*
- Required matrix now C++23-only (c++17/20 impossible with deducing-this; c++26 covered solely by the advisory job). Flag if C++26 should be required instead.

## From Task 2.1 (core port)

- Template leftover: todo test block guards `CXX_MODULE_STD` with `BEMAN_EXEMPLAR_USE_MODULES` (wrong project name); rename. *template*
- Module (`USE_MODULES=ON`) build path is wired but not exercised by `make test`; add a CI matrix entry.

## From Task 4.3 (engineering reality)

- Verify P2688/P1371 pattern-matching status against the current WG21 paper trail before publication (text hedged "as of this writing").
- All `# MEASURE:` / `<!-- MEASURE -->` flags in `docs/notes/engineering-reality-sections.md`: re-measure compile-time and codegen claims against beman.tree_algorithms before quoting numbers.

## From Task 4.2 (D-paper)

- Verify the `std::indirect` paper number cited in References (P3019) before publication.
- Replace `DnnnnR0` once a real number is assigned; synopsis code block carries a SYNOPSIS-TRANSCLUDE-TODO for Task 4.4.

## Deferred by design

- Foldable/Traversable article material (source lines 572-796) earmarked for a follow-up article; not in the minimal core.
- Vendor the `papers/wg21` Pandoc framework (git subtree, as in the fixpoint repo) when the D-paper needs real builds; the draft lives in `papers/` as plain Pandoc-WG21 markdown until then.
