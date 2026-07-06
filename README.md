# beman.tree_algorithms: Recursive tree algorithms: fold_fix, unfold_fix, refold over fixed-point trees

<!--
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
-->

<!-- markdownlint-disable line-length -->
[![Library Status](https://raw.githubusercontent.com/bemanproject/beman/refs/heads/main/images/badges/beman_badge-beman_library_under_development.svg)](https://github.com/bemanproject/beman/blob/main/docs/beman_library_maturity_model.md#the-beman-library-maturity-model)
[![Continuous Integration Tests](https://github.com/bemanproject/tree_algorithms/actions/workflows/ci_tests.yml/badge.svg)](https://github.com/bemanproject/tree_algorithms/actions/workflows/ci_tests.yml)
[![Lint Check (pre-commit)](https://github.com/bemanproject/tree_algorithms/actions/workflows/pre-commit-check.yml/badge.svg)](https://github.com/bemanproject/tree_algorithms/actions/workflows/pre-commit-check.yml)
[![Coverage](https://coveralls.io/repos/github/bemanproject/tree_algorithms/badge.svg?branch=main)](https://coveralls.io/github/bemanproject/tree_algorithms?branch=main)
![Standard Target](https://github.com/bemanproject/beman/blob/main/images/badges/cpp29.svg)
<!-- markdownlint-restore -->

`beman.tree_algorithms` is (... TODO: description).

**Implements**: `std::todo` proposed in [TODO (DnnnnR0)](https://wg21.link/DnnnnR0).

**Status**: [Under development and not yet ready for production use.](https://github.com/bemanproject/beman/blob/main/docs/beman_library_maturity_model.md#under-development-and-not-yet-ready-for-production-use)

## License

`beman.tree_algorithms` is licensed under the Apache License v2.0 with LLVM Exceptions.

## Usage

TODO

Full runnable examples can be found in [`examples/`](examples/).

## Dependencies

### Build Environment

This project requires at least the following to build:

* A C++ compiler that conforms to the C++23 standard or greater
* CMake 3.30 or later
* (Test Only) GoogleTest

You can disable building tests by setting CMake option `BEMAN_TREE_ALGORITHMS_BUILD_TESTS` to
`OFF` when configuring the project.

### Supported Platforms

The library requires C++23 with deducing-this (explicit object parameters), so the toolchain floor is GCC 14 and Clang 18.

| Compiler   | Version | C++ Standards | Standard Library  |
|------------|---------|---------------|-------------------|
| GCC        | 16-14   | C++23         | libstdc++         |
| Clang      | 22-18   | C++23         | libstdc++, libc++ |
| AppleClang | latest  | C++23         | libc++            |
| MSVC       | latest  | C++23         | MSVC STL          |

C++26 is additionally exercised by an advisory (allowed-to-fail) CI job on the newest GCC.

## Development

See the [Contributing Guidelines](CONTRIBUTING.md).

## Integrate beman.tree_algorithms into your project

### Build

You can build tree_algorithms using a CMake workflow preset:

```bash
cmake --workflow --preset gcc-release
```

To list available workflow presets, you can invoke:

```bash
cmake --list-presets=workflow
```

For details on building beman.tree_algorithms without using a CMake preset, refer to the
[Contributing Guidelines](CONTRIBUTING.md).

### Installation

#### Vcpkg

The preferred way to install tree_algorithms is via vcpkg. To do so, after installing vcpkg
itself, you need to add support for the Beman project's [vcpkg
registry](https://github.com/bemanproject/vcpkg-registry) by configuring a
`vcpkg-configuration.json` file (which tree_algorithms [provides](vcpkg-configuration.json)).

Then, simply run `vcpkg install beman-tree-algorithms`.

#### Manual

To install beman.tree_algorithms globally after building with the `gcc-release` preset, you can
run:

```bash
sudo cmake --install build/gcc-release
```

Alternatively, to install to a prefix, for example `/opt/beman`, you can run:

```bash
sudo cmake --install build/gcc-release --prefix /opt/beman
```

This will generate the following directory structure:

```txt
/opt/beman
├── include
│   └── beman
│       └── tree_algorithms
│           ├── tree_algorithms.hpp
│           └── ...
└── lib
    └── cmake
        └── beman.tree_algorithms
            ├── beman.tree_algorithms-config-version.cmake
            ├── beman.tree_algorithms-config.cmake
            └── beman.tree_algorithms-targets.cmake
```

### CMake Configuration

If you installed beman.tree_algorithms to a prefix, you can specify that prefix to your CMake
project using `CMAKE_PREFIX_PATH`; for example, `-DCMAKE_PREFIX_PATH=/opt/beman`.

You need to bring in the `beman.tree_algorithms` package to define the `beman::tree_algorithms` CMake
target:

```cmake
find_package(beman.tree_algorithms REQUIRED)
```

You will then need to add `beman::tree_algorithms` to the link libraries of any libraries or
executables that include `beman.tree_algorithms` headers.

```cmake
target_link_libraries(yourlib PUBLIC beman::tree_algorithms)
```

### Using beman.tree_algorithms

To use `beman.tree_algorithms` in your C++ project,
include an appropriate `beman.tree_algorithms` header from your source code.

```c++
#include <beman/tree_algorithms/tree_algorithms.hpp>
```
