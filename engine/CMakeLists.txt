# Define the core engine library
add_library(engine STATIC
    src/engine.cpp
    src/engine_factory.cpp
    src/resources/atlas_clips.cpp
    src/resources/atlas_clips_loader.cpp
    src/resources/atlas_clips_serializer.cpp
    src/resources/character_registry.cpp
    src/resources/character_sheet_loader.cpp
    src/resources/character_sheet_serializer.cpp
    src/resources/sprite_atlas_loader.cpp
    src/resources/sprite_sheet_clips_loader.cpp
    src/resources/sprite_sheet_clips_serializer.cpp
    src/dialogue/action.cpp
    src/dialogue/expr.cpp
    src/dialogue/loader.cpp
    src/dialogue/validate.cpp
    src/dialogue/validate_refs.cpp
    src/dialogue/query.cpp
    src/dialogue/registry.cpp
    src/dialogue/serialize.cpp
    src/dialogue/system.cpp
    src/quest/loader.cpp
    src/quest/serialize.cpp
    src/quest/validate.cpp
    src/quest/registry.cpp
    src/quest/system.cpp
    src/quest/runner.cpp
    src/item/item.cpp
    src/core/files.cpp
    src/physics/collision.cpp
    src/physics/walkability.cpp
    src/core/game_config.cpp
    src/core/json_schema.cpp
    src/core/json_io.cpp
    src/world/actors/actor_loader.cpp
    src/world/portals/portal.cpp
    src/world/portals/transition_prompt.cpp
    src/anim/anim_sys.cpp
    src/world/camera_system.cpp
    src/world/picking.cpp
    src/world/pathfinding.cpp
    src/dialogue/interact.cpp
    src/input/input_sys.cpp
    src/physics/physics_sys.cpp
    src/render/render_sys.cpp
    src/world/tilemap/loader.cpp
    src/world/tilemap/serialize.cpp
    src/world/tilemap/world_manifest.cpp
    src/world/tilemap/validate.cpp
    src/world/tilemap/walkability.cpp
    src/world/spawn.cpp
    src/world/map_view.cpp
    src/world/transition.cpp
    src/world/update.cpp
    src/ui/nine_patch.cpp
    src/ui/dialog_box.cpp
    src/ui/prompt_box.cpp
    src/ui/inventory_panel.cpp
    src/ui/ui_draw.cpp
    src/debug/debug_overlay.cpp
    src/audio/audio_sys.cpp
)

# Public headers are in the 'include' folder.
target_include_directories(engine PUBLIC include)

# Link dependencies that the engine itself needs
target_link_libraries(engine PUBLIC nlohmann_json::nlohmann_json
                                   PUBLIC nlohmann_json_schema_validator::validator)

# PCH — stable STL + third-party headers that are slow to parse
target_precompile_headers(engine PRIVATE src/pch.hpp)

# engine_factory.cpp references platform::create_platform in engine_platform_glfw.
# In a Unity batch the linker can't skip it; keep it standalone so the linker only
# pulls it in when make_engine() is actually called.
set_source_files_properties(src/engine_factory.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)

# Add the platform sub-module
add_subdirectory(src/platform)

target_compile_options(engine PRIVATE -Wall -Wextra -Wpedantic)
