# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

find_package(Catch2 3 REQUIRED)

add_executable(beman.tree_algorithms.tests.todo)
target_sources(beman.tree_algorithms.tests.todo PRIVATE todo.test.cpp)
target_link_libraries(
    beman.tree_algorithms.tests.todo
    PRIVATE beman::tree_algorithms Catch2::Catch2WithMain
)
if(BEMAN_EXEMPLAR_USE_MODULES)
    set_target_properties(
        beman.tree_algorithms.tests.todo
        PROPERTIES CXX_MODULE_STD ON
    )
endif()

add_executable(beman.tree_algorithms.tests.box)
target_sources(beman.tree_algorithms.tests.box PRIVATE box.test.cpp)
target_link_libraries(
    beman.tree_algorithms.tests.box
    PRIVATE beman::tree_algorithms Catch2::Catch2WithMain
)
if(BEMAN_TREE_ALGORITHMS_USE_MODULES)
    set_target_properties(
        beman.tree_algorithms.tests.box
        PROPERTIES CXX_MODULE_STD ON
    )
endif()

add_executable(beman.tree_algorithms.tests.fix)
target_sources(beman.tree_algorithms.tests.fix PRIVATE fix.test.cpp)
target_link_libraries(
    beman.tree_algorithms.tests.fix
    PRIVATE beman::tree_algorithms Catch2::Catch2WithMain
)
if(BEMAN_TREE_ALGORITHMS_USE_MODULES)
    set_target_properties(
        beman.tree_algorithms.tests.fix
        PROPERTIES CXX_MODULE_STD ON
    )
endif()

add_executable(beman.tree_algorithms.tests.overloaded)
target_sources(
    beman.tree_algorithms.tests.overloaded
    PRIVATE overloaded.test.cpp
)
target_link_libraries(
    beman.tree_algorithms.tests.overloaded
    PRIVATE beman::tree_algorithms Catch2::Catch2WithMain
)
if(BEMAN_TREE_ALGORITHMS_USE_MODULES)
    set_target_properties(
        beman.tree_algorithms.tests.overloaded
        PROPERTIES CXX_MODULE_STD ON
    )
endif()

add_executable(beman.tree_algorithms.tests.recursion_schemes)
target_sources(
    beman.tree_algorithms.tests.recursion_schemes
    PRIVATE recursion_schemes.test.cpp
)
target_link_libraries(
    beman.tree_algorithms.tests.recursion_schemes
    PRIVATE beman::tree_algorithms Catch2::Catch2WithMain
)
if(BEMAN_TREE_ALGORITHMS_USE_MODULES)
    set_target_properties(
        beman.tree_algorithms.tests.recursion_schemes
        PROPERTIES CXX_MODULE_STD ON
    )
endif()

include(Catch)
catch_discover_tests(beman.tree_algorithms.tests.todo)
catch_discover_tests(beman.tree_algorithms.tests.box)
catch_discover_tests(beman.tree_algorithms.tests.fix)
catch_discover_tests(beman.tree_algorithms.tests.overloaded)
catch_discover_tests(beman.tree_algorithms.tests.recursion_schemes)
