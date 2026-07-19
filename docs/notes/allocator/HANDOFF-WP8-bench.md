# WP-8 handoff — benchmarks (default vs monotonic vs pool)

Scope note: this handoff covers the *benchmark* half of WP-8 only (per the managing agent's split: "the paper/prose half is a separate agent that runs after you and consumes your numbers").
No paper, README, or `DECISIONS.md` prose was touched here.

## Landed

- NEW `benchmarks/beman/tree_algorithms/allocator.bench.cpp` — three `TEST_CASE`s (`[allocator][build][!benchmark]`, `[allocator][fold][!benchmark]`, `[allocator][teardown][!benchmark]`) plus the bootstrap case, all over the `BinaryTree`/`BinaryTreeF` representation build.bench.cpp already uses, so the numbers sit next to that file's:
  - **build**: converts an already-built native `BinaryTree` (built once, outside the timed region) to its Fix form under default `std::allocator` (`to_fix`), a fresh `std::pmr::monotonic_buffer_resource` (`pmr::to_fix`), and a fresh `std::pmr::unsynchronized_pool_resource` (`pmr::to_fix`) — resource construction happens inside the timed callable, once per sample, per the plan's explicit instruction that standing the resource up is itself part of what is measured. Naive `unique_ptr` and `shared_ptr` (new/delete) builds over the same range-split shape are the baselines; `std::indirect` is compiled in only where `__cpp_lib_indirect` is available (not on this run's toolchain).
  - **fold**: folds the same tree (built once, outside the timed region, one Fix tree per allocator plus the native `shared_ptr` tree) via `fold_map`, to show reads are resource-independent.
  - **teardown**: the headline. `BENCHMARK_ADVANCED` + `Catch::Benchmark::Chronometer::measure` + `Catch::Benchmark::destructable_object<T>` (`catch_constructor.hpp`) isolates destruction from construction: `meter.runs()` trees are built in an untimed setup phase, then only their destructors are timed. Rows: `Fix<Box>` under default/monotonic/pool, plus naive `shared_ptr`/`unique_ptr` (new/delete) baselines.
  - Every `BENCHMARK`/`BENCHMARK_ADVANCED` section is preceded by `REQUIRE`s (via `fold_map`, generic over the allocator since `BinaryTreeF`'s `functor_typeclass`/`layer_fold_typeclass` are registered allocator-generically per WP-5) proving every build route folds to the same closed-form sum before anything is timed.
- `benchmarks/beman/tree_algorithms/CMakeLists.txt` — added `allocator` to the `_runtime_benches` list (so it gets the same `add_executable`/`target_link_libraries`/`CXX_MODULE_STD` treatment as `fold`/`build`, no new logic) and to `bench_runtime_all`'s dependency list.
- `Makefile` — added one `@echo` + one invocation line to the `bench` target, running the new binary with the same `"[!benchmark]"` filter as the other two.
- Did **not** touch `build.bench.cpp`, `fold.bench.cpp`, `naive_trees.hpp`, `inline_layer.hpp`, `fix_unique_tree.hpp`, any header, module interface, or anything under `infra/` — confirmed by `git diff --stat` (below).
- Did **not** touch the paper, README, or `DECISIONS.md` — that is the prose half's job.

## Interface decided

Nothing here is a public library interface; this is benchmark-only code consuming the surface WP-1..WP-7 already shipped:

- Allocator-aware Fix build: `beman::tree_algorithms::to_fix(native)` (default `std::allocator`) vs `pmr::to_fix(pmr::allocator_type(&resource), native)` (monotonic/pool) — both return the *same shape* tree (`BinaryTreeFix<T>` vs `pmr::BinaryTreeFix<T>`), read uniformly through `fold_map<Result>(map_fn, combine, identity, tree)`, which resolves generically over the layer's `Allocator` template parameter (confirmed working exactly as WP-5's handoff predicted: "lookup-based fold ... works out of the box ... as long as the algebra's parameter type is written generically" — `fold_map`'s own algebra is generic, so no hand-written per-allocator algebra was needed anywhere in this file).
- Teardown isolation idiom (for any future benchmark that needs construct/destroy split): `BENCHMARK_ADVANCED("name")(Catch::Benchmark::Chronometer meter) { std::vector<Catch::Benchmark::destructable_object<T>> storage(meter.runs()); /* untimed: storage[i].construct(...) */ meter.measure([&](int i){ storage[i].destruct(); }); }`. `storage_for<T>` (auto-destructing) is for isolating *construction*; `destructable_object<T>` (manual `destruct()`, no auto-destruct) is for isolating *destruction* — the two are not interchangeable (`storage_for<T>::destruct()` is template-disabled; only `destructable_object<T>::destruct()` compiles). Worth carrying forward if a future WP wants a construction-only row too.
- Resource-lifetime pattern for a pmr tree returned *by value* from a benchmark callable (needed because `std::pmr::monotonic_buffer_resource`/`unsynchronized_pool_resource` are neither copyable nor movable, so a per-sample resource cannot simply be a captured local that outlives the return): a small aggregate (`BuiltMonoTree`/`BuiltPoolTree`) bundling a `std::unique_ptr<Resource>` *before* the `PmrFixed tree` member, so reverse-declaration-order destruction destroys the tree (which deallocates through the resource) before the resource itself is freed.

## Verified

- `make compile` (default/Asan config): clean, no warnings. One fix needed along the way: `bench::sum` is overloaded only for `bench::UniqueTree`/`bench::IndirectTree`, not `BinaryTree<T>`; the native-shared_ptr build-baseline `REQUIRE` was switched to `fold_map<std::int64_t>(id, add, std::int64_t{0}, build_native_range(whole))`.
- `make test`: **94/94**, 0 regressions (same count as WP-7's handoff — this WP added no test files, only a benchmark, which `make test`/`ctest` does not register).
- `clang-format -i` then `clang-format --dry-run -Werror`: clean.
- `grep` for banned spellings (`Fix{`, `Box{`, `Identity{`, `#pragma once`, `using namespace`, `cata`/`ana`/`hylo`) and relative includes: no matches.
- `git diff --stat`: `Makefile` (+2), `benchmarks/beman/tree_algorithms/CMakeLists.txt` (+3/-1); new file `benchmarks/beman/tree_algorithms/allocator.bench.cpp`. Nothing under `infra/`, nothing outside the benchmark directory + its two wiring points.
- `make compile-headers`: not run — no header changed (this WP added only a `.cpp` and build-wiring; the compile-headers gate is for headers).
- **`make bench` ran to completion** (Bench config, optimized/unsanitized) and produced numbers for all three benchmark binaries (`fold`, `build`, `allocator`); the new binary's law-first `REQUIRE`s all passed: **"All tests passed (12 assertions in 3 test cases)"**.

### Machine/run caveats (read before trusting any single number below)

- **Single run, not idle.** `ps aux` taken just before the run started showed unrelated compilation processes from other sessions/repos active (`load average: 6.72, 7.83, 8.56` on a 20-core box). This is not specific to the allocator rows — every row in this run, including the pre-existing `fold`/`build` benchmarks, shows the same symptom: several rows have a standard deviation on the same order as, or larger than, the mean (e.g. `fold.bench.cpp`'s own `hand recursion (shared_ptr)` row: mean 45.7 ms, std dev 14.3 ms, ≈31%). Treat every number below as directional, not a clean isolated measurement; a re-run on a quiet machine, and ideally several runs, would tighten these considerably.
- **Build/Fold sections use `kBuildNodes = 2^16 - 1` (65 535 nodes)**, matching `build.bench.cpp`'s own constant, so those two sections are directly comparable to that file's numbers.
- **Teardown deliberately uses a smaller `kTeardownNodes = 2^12 - 1` (4 095 nodes).** `BENCHMARK_ADVANCED`'s untimed setup phase (building `meter.runs()` full trees) reruns at every calibration doubling *and* every one of Catch2's (default 100) samples; at the full 65 535-node size this would have made the teardown rows spend far more wall-clock time standing trees up than the destructor calls they measure. This is a within-file, within-WP judgment call (the brief said "you may adjust, but justify in the handoff") — flagged as DEV-A07 below. Per-node figures below normalize across the two sizes so the comparison stays meaningful.
- **A plain `BENCHMARK` row measures construct *and* destroy together**, not construct alone: Catch2's callable-returning form materializes and destroys the return value within one full expression inside the timed loop (`catch_optimizer.hpp`'s `invoke_deoptimized`/`deoptimize_value`), confirmed by reading the Catch2 sources vendored under this toolchain. This is true of *every* build row in this codebase, old and new (`build.bench.cpp`'s rows have always paid this cost too) — the comparison *between* rows stays honest, but "Build" numbers below should be read as "build, use once, and let go," not "build alone." The dedicated Teardown section, using the advanced form, is what isolates destruction on its own.

### Raw numbers (`make bench`, Bench config, gcc, `-O3`, unsanitized)

Full output was captured verbatim; the allocator section follows (samples=100, iterations=1 per sample throughout — Catch2's own calibration choice, not something this file requested):

```
==== allocator: default vs monotonic vs pool, build / fold / teardown ====
allocator: build a large tree under default vs monotonic vs pool
-------------------------------------------------------------------------------
benchmark name                       samples       iterations    est run time
                                     mean          low mean      high mean
                                     std dev       low std dev   high std dev
-------------------------------------------------------------------------------
to_fix (std::allocator, default)               100             1     1.16793 s
                                        15.5867 ms     14.965 ms    16.4135 ms
                                        3.65347 ms    2.95273 ms    4.71684 ms

pmr::to_fix (monotonic_buffer_resource,
fresh per sample)                              100             1     1.41695 s
                                        6.79885 ms    6.69484 ms    6.90212 ms
                                        527.826 us     461.57 us    618.754 us

pmr::to_fix (unsynchronized_pool_resource,
fresh per sample)                              100             1     1.85406 s
                                         19.057 ms     18.763 ms    19.5587 ms
                                        1.91776 ms    1.29571 ms     3.4703 ms

hand build (shared_ptr, new/delete
baseline)                                      100             1     1.91662 s
                                        21.4755 ms    20.0957 ms    22.9972 ms
                                        7.37431 ms    6.33783 ms    9.21466 ms

hand build (unique_ptr, new/delete
baseline)                                      100             1    910.115 ms
                                        6.07785 ms    5.70929 ms    6.45262 ms
                                        1.89487 ms    1.64389 ms    2.27662 ms

allocator: fold reads are resource-independent
-------------------------------------------------------------------------------
fold_map / shared_ptr native (baseline)        100             1    869.407 ms
                                        5.03708 ms    4.41272 ms    5.76923 ms
                                        3.43604 ms    2.92221 ms    4.01824 ms

fold_map / Fix<Box> (std::allocator)           100             1    206.162 ms
                                        2.28602 ms    1.82239 ms    2.87098 ms
                                        2.63638 ms    2.20313 ms    3.17751 ms

fold_map / Fix<Box> (monotonic_buffer_resource) 100            1    638.632 ms
                                        5.35326 ms    4.54263 ms    6.30094 ms
                                        4.48273 ms     3.9166 ms    5.27123 ms

fold_map / Fix<Box> (unsynchronized_pool_resource) 100         1    376.601 ms
                                        8.47919 ms    7.51091 ms    9.60865 ms
                                        5.31008 ms    4.53884 ms    6.41668 ms

allocator: teardown a tree under default vs monotonic vs pool
-------------------------------------------------------------------------------
teardown Fix<Box> (std::allocator)             100             1     30.501 ms
                                        224.285 us    136.084 us    397.875 us
                                        620.017 us    363.537 us    939.153 us

teardown Fix<Box> (monotonic_buffer_resource)  100             1    11.1837 ms
                                        28.4931 us    26.7168 us    32.0534 us
                                        12.3008 us    6.81279 us    20.1071 us

teardown Fix<Box> (unsynchronized_pool_resource) 100           1    22.4765 ms
                                        325.109 us    184.178 us    599.824 us
                                        965.876 us    547.128 us    1.43688 ms

teardown shared_ptr tree (new/delete baseline) 100             1    44.3638 ms
                                        319.253 us    179.079 us    584.479 us
                                        944.776 us    554.728 us    1.50933 ms

teardown unique_ptr tree (new/delete baseline) 100             1    75.0567 ms
                                        890.245 us    630.216 us    1.29383 ms
                                        1.63046 ms    1.16496 ms    2.20844 ms

===============================================================================
All tests passed (12 assertions in 3 test cases)
```

(The pre-existing `fold`/`build` benchmark tables also ran and passed — "All tests passed (9 assertions in 1 test case)" and "All tests passed (8 assertions in 3 test cases)" respectively — reproduced only in the full captured log, not repeated here since this WP did not change those files or their numbers' meaning.)

### Reading the numbers (per-node, normalized, from the means above)

**Build** (65 535 nodes; ns/node = mean ÷ node count):

| route | mean | ns/node | vs. default |
|---|---|---|---|
| `to_fix` (`std::allocator`, default) | 15.59 ms | 237.8 ns | 1.00× |
| `pmr::to_fix` (monotonic, fresh/sample) | 6.80 ms | 103.7 ns | **2.29× faster** |
| `pmr::to_fix` (pool, fresh/sample) | 19.06 ms | 290.8 ns | 1.22× slower |
| hand build, shared_ptr (new/delete) | 21.48 ms | 327.7 ns | 1.38× slower |
| hand build, unique_ptr (new/delete) | 6.08 ms | 92.7 ns | 2.56× faster |

**Fold** (65 535 nodes; read-only, no allocation):

| route | mean | ns/node |
|---|---|---|
| `fold_map` / shared_ptr native | 5.04 ms | 76.9 ns |
| `fold_map` / `Fix<Box>` (default) | 2.29 ms | 34.9 ns |
| `fold_map` / `Fix<Box>` (monotonic) | 5.35 ms | 81.7 ns |
| `fold_map` / `Fix<Box>` (pool) | 8.48 ms | 129.4 ns |

**Teardown** (4 095 nodes — headline):

| route | mean | ns/node | vs. default |
|---|---|---|---|
| `Fix<Box>` (`std::allocator`, default) | 224.3 us | 54.8 ns | 1.00× |
| `Fix<Box>` (monotonic) | 28.5 us | 7.0 ns | **7.87× faster** |
| `Fix<Box>` (pool) | 325.1 us | 79.4 ns | 1.45× slower |
| shared_ptr baseline (new/delete) | 319.3 us | 77.9 ns | 1.42× slower |
| unique_ptr baseline (new/delete) | 890.2 us | 217.4 ns | 3.97× slower |

**Plain-language reading:**

- **Teardown is where the thesis lands cleanly.** `monotonic_buffer_resource` teardown is **~7.9× faster** than default `std::allocator` teardown, because a monotonic resource's `deallocate` is a no-op — the tree's destructor still walks and destroys every value, but pays no `free()` per node; the whole arena drops at once, later, un-timed. This is the strongest, least-noisy signal in the run (its std dev, 12.3 us, is small relative to its 28.5 us mean, unlike most other rows here).
- **`unsynchronized_pool_resource` did *not* beat default `std::allocator`, in either build or teardown, in this run** — it was ~1.2–1.45× *slower*. This is a genuine, if initially counterintuitive, finding, and the most likely explanation is methodological, not a property of pool resources in general: per the plan's explicit instruction, a *fresh* pool resource is constructed for every sample, so every sample pays the pool's cold-start cost (populating empty per-size-class free lists from its upstream) with no chance to amortize that cost across reuse — the scenario a pool resource is actually designed for (build/tear down many trees against *one* long-lived pool) is not what any row here measures. A follow-up benchmark with one resource reused across many build/teardown cycles would very plausibly reverse this; see Open Questions below.
- **Fold ("reads are resource-independent") is the noisiest section of this run** and did not cleanly show the flat line the thesis predicts (2.29 ms / 5.35 ms / 8.48 ms across default/monotonic/pool — up to ~3.7× spread). Every fold row's std dev is on the same order as its mean (e.g. default: mean 2.29 ms, std dev 2.64 ms), which is the signature of a measurement dominated by system noise rather than real algorithmic cost — expected at this tree size, where the actual read work is sub-millisecond and this run's machine was not idle. This is reported as-is per the gate ("honest measurement, not a favorable result"); a re-run on a quiet machine, or reusing `fold.bench.cpp`'s much larger `kFoldDepth = 20` tree (~1M nodes) for this section, would very likely tighten this to the flat line the earlier, more controlled `fold.bench.cpp` numbers already imply (its `fold_fix / Fix<Box>` vs `hand recursion (Fix<Box> tree)` rows sit close together at that larger, more stable size).
- **Build**: monotonic build is ~2.3× faster than default and close to the cheapest possible baseline (hand-rolled `unique_ptr`, one `malloc` per node, no atomics) — plausible, since a monotonic resource turns "N mallocs" into "a handful of geometrically-growing arena mallocs." The shared_ptr baseline is the slowest route measured, consistent with `allocate_shared`'s combined control-block-plus-object allocation and atomic refcount setup, even single-threaded.

## Deviations

- **DEV-A06** — Plan said (WP-8 file list): "extend `build.bench.cpp`/`fold.bench.cpp`." Done instead: added a new file, `allocator.bench.cpp`, and left both existing files untouched. Why: this was the *managing agent's own dispatch brief* ("Recommended shape... Add a NEW `allocator.bench.cpp` (cleanest — one new target...) Keep `build.bench.cpp`/`fold.bench.cpp` intact"), not an improvisation by this sub-agent — recorded here per the brief's own "you may adjust, but justify in the handoff" instruction, and per the numbering instruction ("Number any new deviation DEV-A06+ in YOUR handoff"). What it frees downstream: `build.bench.cpp`/`fold.bench.cpp` are provably untouched (`git diff --stat` shows zero lines changed in either), so their existing numbers and REQUIREs are exactly what they were before this WP; the paper/prose agent can cite either file's numbers with no risk of this WP having perturbed them.
- **DEV-A07** — Plan/brief said: reuse the existing balanced-tree size (`kBuildNodes = 2^16 - 1`) throughout, "so results are comparable to the existing build benchmark." Done instead: Build and Fold use `kBuildNodes` as specified, but Teardown uses a smaller `kTeardownNodes = 2^12 - 1` (4 095 nodes). Why: `BENCHMARK_ADVANCED`'s untimed setup phase rebuilds `meter.runs()` full trees at every calibration doubling and every sample (traced through `catch_execution_plan.hpp`/`catch_run_for_at_least.hpp`/`catch_chronometer.hpp`); at the full 65 535-node size this multiplies into a very large amount of un-timed but still wall-clock-real tree construction, disproportionate to the destructor calls actually being measured. What it frees/costs downstream: Teardown's absolute numbers are not directly comparable in *ns* to Build/Fold's without normalizing per node (done above, in the "Reading the numbers" tables); the *ratios between allocators within the Teardown section* are unaffected by the smaller N and are the number that matters for the thesis.

## Open questions / warnings for downstream

- **Re-run on an idle machine before treating any specific ratio as final**, especially the Fold section's numbers and the Build section's pool-vs-default ranking — this run's machine had unrelated concurrent compilation load, and several rows (not just the new ones — this is true of the pre-existing `fold.bench.cpp`/`build.bench.cpp` rows too, in this same run) show a standard deviation on the same order as the mean. The monotonic-teardown result (~7.9× faster, low relative std dev) is the one number in this run that looks robust to that noise; treat the rest as directional.
- **The pool-resource rows measure a cold pool, not a warm one.** Every pool-resource row in this file constructs a fresh `std::pmr::unsynchronized_pool_resource` per sample (per the plan's explicit instruction that "the buffer/pool is part of what's measured"), so none of these numbers show what a pool resource is actually for: amortizing allocation cost across many builds/teardowns against one long-lived pool. If the paper wants to make a claim about pool resources specifically (rather than "a fresh pool didn't pay off here"), a follow-up row — one resource, `N` build+teardown cycles inside the timed region — would be needed; not added here because it changes what "fresh resource per sample" means and the current brief was explicit about that phrase.
- **`fold_map`'s allocator-genericity is a real, load-bearing confirmation, not just this file's convenience.** WP-5's handoff flagged, as an open question, that a *hand-written* algebra typed against the 2-argument `BinaryTreeF<T, Result>` alias would not accept a pmr-tagged intermediate layer. This file never hit that wall because it used `fold_map` (whose internal algebra is generic) throughout, for every allocator variant, with zero special-casing — worth citing directly in the paper's WP-8 section as evidence the verbs really are representation/allocator-generic, per Decision 9's own framing.
- **No rose/expression-tree allocator rows.** The brief allowed "and/or `unfold_fix` + `pmr::rose_fmap`/`pmr::expr_fmap` for a Fix-native representation if you prefer an unfold-from-seed row"; this file stuck to the `BinaryTree`/`to_fix` path only, both because the brief's primary recommendation was to reuse `build.bench.cpp`'s exact shape (which is `BinaryTreeF` over the midpoint-split coalgebra) and to keep the new file's scope and runtime bounded. If the prose agent wants an unfold-from-seed allocator row for symmetry with `build.bench.cpp`'s "unfold_fix build (Fix<Box>)" row, `pmr::rose_fmap(a)`/`pmr::expr_fmap(a)` are the documented entry points (`pmr.hpp`, per WP-7's handoff) and would follow the same fresh-resource-per-sample / `BuiltXTree`-bundling pattern this file already established for `to_fix`.
- **`std::indirect` did not compile in on this run's toolchain** (`__cpp_lib_indirect` undefined), so the Build section ran 5 rows, not 6, matching the assertion count reported above (12 = 5 Build + 4 Fold + 3 Teardown REQUIREs). Not a gap specific to this WP — `build.bench.cpp`/`fold.bench.cpp` show the identical toolchain-gated behavior.

## Managing-agent spot-verification (run 2)

Per plan §6.3, the managing agent independently re-ran `make bench` (Bench config, same machine but quieter — unrelated compile load had drained) as the WP-8 spot-check.
The direction of every finding reproduced; the magnitude of the monotonic-teardown advantage did not, confirming the single-run caveat above.
This is the source for the paper's "run 2" teardown column.

Teardown (4,095 nodes), mean per row:

| route | run 1 (this handoff, loaded) | run 2 (spot-verify, quieter) |
|---|---|---|
| default `std::allocator` | 224.3 us | 47.2 us |
| monotonic | 28.5 us | 17.9 us |
| pool | 325.1 us | 93.8 us |

Monotonic-vs-default teardown: 7.9x faster (run 1) vs 2.6x faster (run 2).
Pool slower than default in both (1.45x, 1.99x).
The swing is driven mostly by the default row (224 -> 47 us); monotonic itself is comparatively stable (28.5 -> 17.9 us).
Reading: the monotonic teardown advantage is real and reproducible in direction, unstable in magnitude — report the range, not a single figure.
