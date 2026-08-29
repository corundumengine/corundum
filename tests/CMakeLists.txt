enable_testing()

# The test executable
add_executable(corundum_tests
    main.cpp
    test_actions.cpp
    test_collision.cpp
    test_dialogue.cpp
    test_quest.cpp
    test_audio.cpp
    test_spawn_points.cpp
    test_portals.cpp
    test_sprite.cpp
    test_game_config.cpp
    test_handle_pool.cpp
    test_word_wrap.cpp
    test_tilemap_loader.cpp
    test_character_sheet_loader.cpp
    test_sprite_atlas_loader.cpp
    test_atlas_clips.cpp
    test_world_manifest.cpp
    test_world_transition.cpp
    test_iso_math.cpp
    test_elevation_at.cpp
    test_tilemap_validate.cpp
    test_material_at.cpp
    test_walkability.cpp
    test_picking.cpp
    test_pathfinding.cpp
    test_follow_path.cpp
    test_anim_sys.cpp
    test_camera_system.cpp
    test_entity.cpp
    test_engine_events.cpp
    test_render_state.cpp
    test_engine_lifecycle.cpp
    test_chunk_collision_aggregate.cpp
    test_debug_overlay.cpp
    test_elevation_gate.cpp
    test_portal_elevation.cpp
    test_picking_elevation.cpp
    test_layer_presets.cpp
    test_files.cpp
)

# FontAtlas tests reach into the internal engine/src/platform/glfw header and
# compile font_atlas.cpp directly so the test target doesn't drag in the full
# GLFW backend. When corundum is consumed as a vendored library (downstream
# projects expose only engine/include/ on their include path) those internals
# aren't reachable — gate the test off so the build doesn't break on sync.
set(_font_atlas_header "${CMAKE_CURRENT_SOURCE_DIR}/../engine/src/platform/glfw/font_atlas.hpp")
set(_font_atlas_fixture "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/fonts/NotoSans.ttf")
if(EXISTS "${_font_atlas_header}" AND EXISTS "${_font_atlas_fixture}")
    target_sources(corundum_tests PRIVATE
        test_font_atlas.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../engine/src/platform/glfw/font_atlas.cpp
    )
    target_link_libraries(corundum_tests PRIVATE freetype)
    target_include_directories(corundum_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../engine/src/platform/glfw
    )
else()
    message(STATUS "FontAtlas tests skipped — internal header or fixture font not reachable from ${CMAKE_CURRENT_SOURCE_DIR}")
endif()

# Link to engine and doctest. engine_platform_null is also pulled in so the
# lifecycle tests can adopt a null Window/GpuContext/Renderer — it adds no
# runtime cost for the rest of the suite because no other test touches it.
# `engine` itself is brought in transitively via engine_platform_null's PUBLIC
# link, so listing it here too would emit the duplicate-library warning.
target_link_libraries(corundum_tests
    PRIVATE
        engine_platform_null
        doctest::doctest
)

# Tilesmith-only test (test_picking_elevation) needs to read the editor's coord helpers.
target_include_directories(corundum_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/tools/editors/tilesmith
)

target_compile_options(corundum_tests PRIVATE -Wall -Wextra -Wpedantic)

# Tell the lifecycle tests where to find their hand-crafted fixtures.
target_compile_definitions(corundum_tests PRIVATE
    CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)

# Multiple test files share identical helper functions (write_file, temp_dir)
# in anonymous namespaces — Unity batching produces redefinition errors.
set_target_properties(corundum_tests PROPERTIES UNITY_BUILD OFF)

# Enable CTest integration
include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(corundum_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
