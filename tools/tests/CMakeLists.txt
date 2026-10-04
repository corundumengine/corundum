# Unit tests for tools/-scoped, non-UI logic: tilesmith coordinate math and palette
# layout (coords.hpp, layer_presets.hpp), spritesmith coordinate math (coords.hpp), the
# toolkit UndoStack, and the tool-config loader. All pure logic with no ImGui/GLFW
# dependency. Same link policy as engine/tests: doctest + engine_platform_null (and
# corundum_gameplay, so a tools test can reach gameplay content the way loom does).

include(${PROJECT_SOURCE_DIR}/cmake/CompilerWarnings.cmake)

# Kept for parity with engine/tests, and for the case where BUILD_TESTING=OFF but
# CORUNDUM_BUILD_TESTS=ON — include(CTest) then never calls enable_testing().
enable_testing()

add_executable(corundum_tools_tests
    main.cpp
    test_picking_elevation.cpp
    test_layer_presets.cpp
    test_spritesmith_coords.cpp
    test_text_buffer.cpp
    test_tilesmith_palette.cpp
    test_tool_config.cpp
    test_undo_stack.cpp
    # Pure (no GLFW/ImGui) toolkit config loader, compiled directly so the test
    # target never links corundum_toolkit's platform surface.
    ${PROJECT_SOURCE_DIR}/tools/toolkit/src/host/tool_config.cpp
)

# Test helpers share anonymous-namespace names with the other suites; opt out of
# unity builds even when a parent project enables them globally.
set_property(TARGET corundum_tools_tests PROPERTY UNITY_BUILD OFF)

target_link_libraries(corundum_tools_tests
    PRIVATE
        corundum_gameplay
        engine_platform_null
        doctest::doctest
)

# Header-only helpers from tilesmith and the toolkit. spritesmith's coords.hpp is included
# by relative path from its test to avoid colliding with tilesmith's same-named file.
target_include_directories(corundum_tools_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/tools/editors/tilesmith
    ${PROJECT_SOURCE_DIR}/tools/toolkit/include
)

corundum_use_cxx23(corundum_tools_tests)
corundum_enable_warnings(corundum_tools_tests)

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(corundum_tools_tests WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
