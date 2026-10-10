# Gameplay tests (doctest). Split from the engine suite so the engine test
# binary links only `engine_platform_null` + doctest, per AGENTS.md — the
# gameplay framework is a separate target now. Everything here links
# `corundum_gameplay`, so the engine suite (`corundum_tests`) stays free of it.
#
# Tests are grouped under engine/tests/gameplay/ mirroring the gameplay
# modules, plus the root/save/world end-to-end tests that build a Runtime.

include(${PROJECT_SOURCE_DIR}/cmake/CompilerWarnings.cmake)

add_executable(corundum_gameplay_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/../main.cpp
    codex/test_codex.cpp
    credits/test_credits.cpp
    dialogue/test_compiled_expr.cpp
    dialogue/test_dialogue.cpp
    dialogue/test_zone_scope.cpp
    item/test_equipment.cpp
    item/test_item_loader.cpp
    item/test_item_serialize.cpp
    location/test_location.cpp
    quest/test_quest.cpp
    root/test_character_sheet_toggle.cpp
    root/test_engine_events.cpp
    root/test_engine_lifecycle.cpp
    root/test_hub.cpp
    root/test_inventory_toggle.cpp
    root/test_journal_toggle.cpp
    root/test_modal_top_draw.cpp
    root/test_mouse_ui.cpp
    root/test_pause_menu.cpp
    root/test_phase_d_screens.cpp
    root/test_screen_registry.cpp
    root/test_session_reset.cpp
    root/test_settings_persistence.cpp
    root/test_title_saveload_gameover.cpp
    save/test_save.cpp
    screens/test_character_sheet.cpp
    screens/test_dialog_reveal.cpp
    screens/test_hub_panels.cpp
    screens/test_hub_tabs.cpp
    screens/test_hud_strip.cpp
    screens/test_journal.cpp
    screens/test_screen_draw.cpp
    shop/test_shop.cpp
    test_gameplay_umbrella_header.cpp
    world/test_ui_stack.cpp
)

# Test helpers share anonymous-namespace names with the engine suite; opt out of
# unity builds even when a parent project enables them globally.
set_property(TARGET corundum_gameplay_tests PROPERTY UNITY_BUILD OFF)

# Shared fixtures (temp_dir.hpp, world_transition_fixtures.hpp,
# ui/recording_renderer.hpp) live at the engine/tests root and are included with
# a bare "..." path from both the gameplay folders and the moved root tests.
target_include_directories(corundum_gameplay_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(corundum_gameplay_tests
    PRIVATE
        corundum_gameplay
        engine_platform_null
        doctest::doctest
)

corundum_use_cxx23(corundum_gameplay_tests)
corundum_enable_warnings(corundum_gameplay_tests)

# The lifecycle test reaches into the shared headless fixtures.
target_compile_definitions(corundum_gameplay_tests PRIVATE
    CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR="${PROJECT_SOURCE_DIR}/engine/tests/fixtures"
)

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(corundum_gameplay_tests WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
