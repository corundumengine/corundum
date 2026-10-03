# Unit tests (doctest). The suite links engine_platform_null + doctest only —
# never GLFW/ImGui/sokol. `engine` arrives transitively via the null backend's
# PUBLIC link, so listing it here too would emit the duplicate-library warning.
#
# Tests are grouped into per-module subfolders mirroring the top-level modules
# under engine/include/corundum/. Root-header and end-to-end Engine tests live
# in root/. Five tests compile internal engine headers directly and are gated
# separately below.

include(${PROJECT_SOURCE_DIR}/cmake/CompilerWarnings.cmake)

enable_testing()

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

    # gameplay
    gameplay/codex/test_codex.cpp
    gameplay/dialogue/test_compiled_expr.cpp
    gameplay/dialogue/test_dialogue.cpp
    gameplay/dialogue/test_zone_scope.cpp
    gameplay/item/test_item_loader.cpp
    gameplay/item/test_item_serialize.cpp
    gameplay/location/test_location.cpp
    gameplay/quest/test_quest.cpp
    gameplay/screens/test_dialog_reveal.cpp
    gameplay/screens/test_hub_tabs.cpp
    gameplay/screens/test_hud_strip.cpp
    gameplay/screens/test_journal.cpp
    gameplay/screens/test_screen_draw.cpp
    gameplay/shop/test_shop.cpp

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
    render/test_render_state.cpp
    render/test_ui_assets.cpp
    render/test_world_walkability.cpp

    # root
    root/test_engine_events.cpp
    root/test_engine_lifecycle.cpp
    root/test_engine_options.cpp
    root/test_hub.cpp
    root/test_inventory_toggle.cpp
    root/test_journal_toggle.cpp
    root/test_pause_menu.cpp
    root/test_phase_d_screens.cpp
    root/test_player_absent.cpp
    root/test_screen_registry.cpp
    root/test_umbrella_header.cpp

    # save
    save/test_save.cpp

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
    world/test_material_at.cpp
    world/test_pathfinding.cpp
    world/test_pathfinding_world.cpp
    world/test_picking.cpp
    world/test_portals.cpp
    world/test_spawn_points.cpp
    world/test_tilemap_loader.cpp
    world/test_tilemap_validate.cpp
    world/test_transition_prompt.cpp
    world/test_ui_stack.cpp
    world/test_world_manifest.cpp
    world/test_world_transition.cpp
)

# The test executable
add_executable(corundum_tests ${TEST_SOURCES})

# Test sources include shared helpers (temp_dir.hpp) with a bare "..." path.
target_include_directories(corundum_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# FontAtlas tests reach into the internal engine/src/platform/glfw header and
# compile font_atlas.cpp directly so the test target doesn't drag in the full
# GLFW backend. When corundum is consumed as a vendored library (downstream
# projects expose only engine/include/ on their include path) those internals
# aren't reachable — gate the test off so the build doesn't break on sync.
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

# render_scale.hpp is a pure internal header; gate the test off when consuming corundum as a
# vendored library, same rationale as the FontAtlas tests above.
set(_render_scale_header "${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/glfw/render_scale.hpp")
if(EXISTS "${_render_scale_header}")
  target_sources(corundum_tests PRIVATE platform/test_render_scale.cpp)
  target_include_directories(corundum_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/glfw
    )
else()
  message(STATUS "render-scale tests skipped — internal header not reachable from ${CMAKE_CURRENT_SOURCE_DIR}")
endif()

# texture_byte_size.hpp is likewise a pure internal header (the sokol upload wrapper that consumes it
# is not linkable from tests); gate it the same way.
set(_texture_byte_size_header "${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/glfw/texture_byte_size.hpp")
if(EXISTS "${_texture_byte_size_header}")
  target_sources(corundum_tests PRIVATE platform/test_texture_byte_size.cpp)
  target_include_directories(corundum_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/glfw
    )
else()
  message(STATUS "texture byte-size tests skipped — internal header not reachable from ${CMAKE_CURRENT_SOURCE_DIR}")
endif()

# encoding.hpp is a pure internal header (the JSON spellings shared by the tilemap loader and
# serializer); gate the test off when consuming corundum as a vendored library.
set(_tilemap_encoding_header "${CMAKE_CURRENT_SOURCE_DIR}/../src/world/tilemap/encoding.hpp")
if(EXISTS "${_tilemap_encoding_header}")
  target_sources(corundum_tests PRIVATE world/test_tilemap_encoding.cpp)
  target_include_directories(corundum_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/world/tilemap
    )
else()
  message(STATUS "tilemap encoding tests skipped — internal header not reachable from ${CMAKE_CURRENT_SOURCE_DIR}")
endif()

# warn_log.hpp is an internal engine header; gate the test off when consuming
# corundum as a vendored library.
set(_warn_log_header "${CMAKE_CURRENT_SOURCE_DIR}/../src/core/warn_log.hpp")
if(EXISTS "${_warn_log_header}")
  target_sources(corundum_tests PRIVATE core/test_warn_log.cpp)
  target_include_directories(corundum_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/core
    )
else()
  message(STATUS "warn_log tests skipped — internal header not reachable from ${CMAKE_CURRENT_SOURCE_DIR}")
endif()

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
