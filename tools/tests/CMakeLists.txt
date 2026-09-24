# Unit tests for tools/-scoped header-only helpers — currently tilesmith's coordinate math
# (coords.hpp) and layer-preset helpers (layer_presets.hpp), both pure logic with no ImGui/GLFW
# dependency. Same link policy as engine/tests: doctest + engine_platform_null only.

include(${PROJECT_SOURCE_DIR}/cmake/CompilerWarnings.cmake)

# Kept for parity with engine/tests, and for the case where BUILD_TESTING=OFF but
# CORUNDUM_BUILD_TESTS=ON — include(CTest) then never calls enable_testing().
enable_testing()

add_executable(corundum_tools_tests
    main.cpp
    test_picking_elevation.cpp
    test_layer_presets.cpp
)

target_link_libraries(corundum_tools_tests
    PRIVATE
        engine_platform_null
        doctest::doctest
)

# Reads tilesmith's coord/layer-preset helpers directly — header-only, no tilesmith .cpp linked.
target_include_directories(corundum_tools_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/tools/editors/tilesmith
)

corundum_use_cxx23(corundum_tools_tests)
corundum_enable_warnings(corundum_tools_tests)

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(corundum_tools_tests WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
