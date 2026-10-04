# Core engine library.
#
# `engine` is the pure C++23 core (no GLFW/ImGui/sokol). The windowing +
# rendering backend lives in src/platform and is added as a separate library
# (engine_platform_glfw / engine_platform_null) — see src/platform/CMakeLists.txt.
# The optional gameplay framework is a separate library (`corundum_gameplay`)
# layered above `engine`; the engine runtime never links it.

include(${PROJECT_SOURCE_DIR}/cmake/CompilerWarnings.cmake)

# All .cpp under engine/src except src/platform/ (the GLFW backend is built
# as a separate library with its own sources), src/gameplay/ (the gameplay
# framework, built as corundum_gameplay below) and src/engine_factory.cpp
# (kept standalone — see below).
set(ENGINE_SOURCES
    src/animation/animation_system.cpp
    src/audio/audio_system.cpp
    src/core/environment.cpp
    src/core/files.cpp
    src/core/game_config.cpp
    src/core/json_io.cpp
    src/core/json_schema.cpp
    src/core/schema_version.cpp
    src/core/user_data_dir.cpp
    src/core/verify.cpp
    src/debug/debug_overlay.cpp
    src/engine.cpp
    src/engine_factory.cpp
    src/engine_options.cpp
    src/input/bindings.cpp
    src/input/input_intent.cpp
    src/input/input_mapper.cpp
    src/input/input_system.cpp
    src/input/physical_input.cpp
    src/physics/collision.cpp
    src/physics/physics_system.cpp
    src/physics/walkability.cpp
    src/render/render_system.cpp
    src/save/save.cpp
    src/settings/user_settings.cpp
    src/sprites/atlas_clips.cpp
    src/sprites/atlas_clips_loader.cpp
    src/sprites/atlas_clips_serializer.cpp
    src/sprites/character_registry.cpp
    src/sprites/character_sheet_loader.cpp
    src/sprites/character_sheet_serializer.cpp
    src/sprites/sprite_atlas_loader.cpp
    src/sprites/sprite_sheet_clips_loader.cpp
    src/sprites/sprite_sheet_clips_serializer.cpp
    src/ui/input_glyph.cpp
    src/ui/menu.cpp
    src/ui/nine_patch.cpp
    src/ui/prompt_box.cpp
    src/ui/settings.cpp
    src/ui/toast.cpp
    src/ui/ui_draw.cpp
    src/world/actors/actor_loader.cpp
    src/world/camera.cpp
    src/world/map_view.cpp
    src/world/pathfinding.cpp
    src/world/picking.cpp
    src/world/portals/portal.cpp
    src/world/portals/transition_prompt.cpp
    src/world/spawn.cpp
    src/world/tilemap/loader.cpp
    src/world/tilemap/serialize.cpp
    src/world/tilemap/validate.cpp
    src/world/tilemap/walkability.cpp
    src/world/tilemap/world_manifest.cpp
    src/world/transition.cpp
    src/world/update.cpp
)

add_library(engine STATIC ${ENGINE_SOURCES})

# engine_factory must stay its own object: a parent project may enable unity builds
# globally, and batching it with engine.cpp would fuse the factory entry point into
# the wrong translation unit. Only this file opts out, so the rest of the engine can
# still batch when the developer asks for it.
set_source_files_properties(src/engine_factory.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)

# Public headers are in the 'include' folder; 'src' is private so internal
# helpers (e.g. src/core/warn_log.hpp) are shareable between engine TUs.
target_include_directories(engine PUBLIC include PRIVATE src)

# C++23 is PUBLIC so it propagates to consumers; the warning policy is applied
# explicitly per-target (PRIVATE flags, never inherited through links).
corundum_use_cxx23(engine)
corundum_enable_warnings(engine)
corundum_limit_stack_frames(engine)

# Link dependencies that the engine itself needs
target_link_libraries(engine PUBLIC nlohmann_json::nlohmann_json
                                   PUBLIC nlohmann_json_schema_validator::validator)

# The reusable-but-optional gameplay framework (dialogue, quests, items, codex,
# shops, locations and their screens). It links the engine runtime, which in turn
# must never link it back — the `layering` ctest enforces the boundary at source
# level. Consumers that only need the engine runtime link `engine` alone.
set(GAMEPLAY_SOURCES
    src/gameplay/codex/loader.cpp
    src/gameplay/codex/registry.cpp
    src/gameplay/dialogue/action.cpp
    src/gameplay/dialogue/compiled_expr.cpp
    src/gameplay/dialogue/conversation.cpp
    src/gameplay/dialogue/loader.cpp
    src/gameplay/dialogue/query.cpp
    src/gameplay/dialogue/registry.cpp
    src/gameplay/dialogue/serialize.cpp
    src/gameplay/dialogue/validate.cpp
    src/gameplay/dialogue/validate_refs.cpp
    src/gameplay/gameplay.cpp
    src/gameplay/item/item.cpp
    src/gameplay/item/serialize.cpp
    src/gameplay/location/loader.cpp
    src/gameplay/location/registry.cpp
    src/gameplay/quest/loader.cpp
    src/gameplay/quest/registry.cpp
    src/gameplay/quest/runner.cpp
    src/gameplay/quest/serialize.cpp
    src/gameplay/quest/status.cpp
    src/gameplay/quest/system.cpp
    src/gameplay/quest/validate.cpp
    src/gameplay/runtime.cpp
    src/gameplay/screens/barter.cpp
    src/gameplay/screens/codex.cpp
    src/gameplay/screens/dialog_box.cpp
    src/gameplay/screens/hub_tabs.cpp
    src/gameplay/screens/hud_strip.cpp
    src/gameplay/screens/inventory_panel.cpp
    src/gameplay/screens/journal.cpp
    src/gameplay/screens/loot.cpp
    src/gameplay/screens/map.cpp
    src/gameplay/shop/loader.cpp
    src/gameplay/shop/registry.cpp
)

add_library(corundum_gameplay STATIC ${GAMEPLAY_SOURCES})

# Gameplay headers live under the same public include root as the engine's; 'src'
# is private so gameplay TUs can reach internal engine helpers (core/warn_log.hpp).
target_include_directories(corundum_gameplay PRIVATE src)
target_link_libraries(corundum_gameplay PUBLIC engine)

corundum_use_cxx23(corundum_gameplay)
corundum_enable_warnings(corundum_gameplay)
corundum_limit_stack_frames(corundum_gameplay)

# Add the platform sub-module
add_subdirectory(src/platform)
