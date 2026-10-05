# Unit tests (doctest). The suite links engine_platform_null + doctest only —
# never GLFW/ImGui/sokol. `engine` arrives transitively via the null backend's
# PUBLIC link, so listing it here too would emit the duplicate-library warning.
#
# Tests are grouped into per-module subfolders mirroring the top-level modules
# under engine/include/corundum/. Root-header and end-to-end Engine tests live
# in root/. Five tests compile internal engine headers directly and are gated
# separately below.

include(${PROJECT_SOURCE_DIR}/cmake/CompilerWarnings.cmake)
include(${PROJECT_SOURCE_DIR}/cmake/InternalHeaderTest.cmake)

# All first-party test translation units, grouped by the engine module they
# exercise. The five gated tests are added by the target_sources blocks below.
set(TEST_SOURCES
    main.cpp

    # animation
    animation/test_animation_system.cpp

    # audio
    audio/test_audio.cpp

    # core
    core/test_direction.cpp
    core/test_files.cpp
    core/test_game_config.cpp
    core/test_iso_math.cpp
    core/test_json_io.cpp
    core/test_json_schema.cpp
    core/test_loop_timer.cpp
    core/test_render_resolution.cpp
    core/test_rng.cpp
    core/test_schema_version.cpp
    core/test_user_data_dir.cpp
    core/test_utf8.cpp

    # debug
    debug/test_debug_overlay.cpp

    # entities
    entities/test_actor_id_table.cpp
    entities/test_entity.cpp

    # gameplay tests are built into corundum_gameplay_tests by
    # engine/tests/gameplay/CMakeLists.txt, not this binary.

    # input
    input/test_action_resolver.cpp
    input/test_actions.cpp
    input/test_bindings.cpp
    input/test_input_intent.cpp
    input/test_input_mapper.cpp
    input/test_input_system.cpp
    input/test_physical_input.cpp

    # physics
    physics/test_collision.cpp
    physics/test_elevation_gate.cpp
    physics/test_follow_path.cpp
    physics/test_portal_elevation.cpp
    physics/test_walkability.cpp

    # platform
    platform/test_handle.cpp
    platform/test_texture_slot_table.cpp

    # render
    render/test_chunk_collision_aggregate.cpp
    render/test_font_families.cpp
    render/test_render_state.cpp
    render/test_ui_assets.cpp
    render/test_world_walkability.cpp

    # root
    root/test_engine_options.cpp
    root/test_player_absent.cpp
    root/test_umbrella_header.cpp

    # save tests live in the gameplay binary (they build a Gameplay).

    # settings
    settings/test_user_settings.cpp

    # sprites
    sprites/test_anim_id.cpp
    sprites/test_atlas_clips.cpp
    sprites/test_character_registry.cpp
    sprites/test_character_sheet_loader.cpp
    sprites/test_character_sheet_serializer.cpp
    sprites/test_sprite.cpp
    sprites/test_sprite_atlas_loader.cpp
    sprites/test_sprite_sheet_clips.cpp

    # ui
    ui/test_input_glyph.cpp
    ui/test_menu.cpp
    ui/test_settings.cpp
    ui/test_toast.cpp
    ui/test_ui_draw.cpp
    ui/test_word_wrap.cpp

    # world
    world/test_camera.cpp
    world/test_elevation_at.cpp
    world/test_interaction_target.cpp
    world/test_material_at.cpp
    world/test_pathfinding.cpp
    world/test_pathfinding_world.cpp
    world/test_picking.cpp
    world/test_portals.cpp
    world/test_spawn_points.cpp
    world/test_tilemap_loader.cpp
    world/test_tilemap_validate.cpp
    world/test_transition_prompt.cpp
    world/test_world_manifest.cpp
    world/test_world_transition.cpp
)

# The test executable
add_executable(corundum_tests ${TEST_SOURCES})

# Test helpers share anonymous-namespace names, so these translation units cannot be
# batched into a unity unit. Opt out even when a parent enables unity builds globally.
set_property(TARGET corundum_tests PROPERTY UNITY_BUILD OFF)

# Test sources include shared helpers (temp_dir.hpp) with a bare "..." path.
target_include_directories(corundum_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# FontAtlas tests reach into the internal engine/src/platform/glfw header and
# compile font_atlas.cpp directly so the test target doesn't drag in the full
# GLFW backend. When corundum is consumed as a vendored library (downstream
# projects expose only engine/include/ on their include path) those internals
# aren't reachable — gate the test off so the build doesn't break on sync.
#
# Kept separate from corundum_add_internal_header_test: this test also checks
# the fixture font, links freetype, and needs two include directories.
set(_font_atlas_header "${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/glfw/font_atlas.hpp")
set(_font_atlas_fixture "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/fonts/NotoSans.ttf")
if(EXISTS "${_font_atlas_header}" AND EXISTS "${_font_atlas_fixture}")
  target_sources(corundum_tests PRIVATE
        platform/test_font_atlas.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/glfw/font_atlas.cpp
    )
  target_link_libraries(corundum_tests PRIVATE freetype)
  target_include_directories(corundum_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/glfw
    )
else()
  message(STATUS "FontAtlas tests skipped — internal header or fixture font not reachable from ${CMAKE_CURRENT_SOURCE_DIR}")
endif()

# These internal-header tests need only the header and its own directory.
corundum_add_internal_header_test(corundum_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/glfw/render_scale.hpp"
    platform/test_render_scale.cpp
    "render-scale tests skipped — internal header not reachable from ${CMAKE_CURRENT_SOURCE_DIR}")

corundum_add_internal_header_test(corundum_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/glfw/texture_byte_size.hpp"
    platform/test_texture_byte_size.cpp
    "texture byte-size tests skipped — internal header not reachable from ${CMAKE_CURRENT_SOURCE_DIR}")

corundum_add_internal_header_test(corundum_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/world/tilemap/encoding.hpp"
    world/test_tilemap_encoding.cpp
    "tilemap encoding tests skipped — internal header not reachable from ${CMAKE_CURRENT_SOURCE_DIR}")

corundum_add_internal_header_test(corundum_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/core/warn_log.hpp"
    core/test_warn_log.cpp
    "warn_log tests skipped — internal header not reachable from ${CMAKE_CURRENT_SOURCE_DIR}")

# Link to engine and doctest. engine_platform_null is also pulled in so the
# lifecycle tests can adopt a null Window/GpuContext/Renderer — it adds no
# runtime cost for the rest of the suite because no other test touches it.
target_link_libraries(corundum_tests
    PRIVATE
        engine_platform_null
        doctest::doctest
)

# C++23 + warning policy propagate from engine_platform_null; keep them explicit.
corundum_use_cxx23(corundum_tests)
corundum_enable_warnings(corundum_tests)

# Tell the lifecycle tests where to find their hand-crafted fixtures.
target_compile_definitions(corundum_tests PRIVATE
    CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR="${PROJECT_SOURCE_DIR}/engine/tests/fixtures"
)

# Enable CTest integration
include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(corundum_tests WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

# Gameplay tests build a separate binary that links corundum_gameplay, so the
# engine binary above stays free of the gameplay framework.
add_subdirectory(gameplay)
