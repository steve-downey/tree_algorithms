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

include(Catch)
catch_discover_tests(beman.tree_algorithms.tests.todo)
