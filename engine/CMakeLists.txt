# Core engine library.
#
# `engine` is the pure C++23 core (no GLFW/ImGui/sokol). The windowing +
# rendering backend lives in src/platform and is added as a separate library
# (engine_platform_glfw / engine_platform_null) — see src/platform/CMakeLists.txt.

include(${PROJECT_SOURCE_DIR}/cmake/CompilerWarnings.cmake)

# All .cpp under engine/src except src/platform/ (the GLFW backend is built
# as a separate library with its own sources) and src/engine_factory.cpp
# (kept standalone — see below).
set(ENGINE_SOURCES
    src/anim/anim_sys.cpp
    src/audio/audio_sys.cpp
    src/core/files.cpp
    src/core/game_config.cpp
    src/core/json_io.cpp
    src/core/json_schema.cpp
    src/debug/debug_overlay.cpp
    src/dialogue/action.cpp
    src/dialogue/expr.cpp
    src/dialogue/interact.cpp
    src/dialogue/loader.cpp
    src/dialogue/query.cpp
    src/dialogue/registry.cpp
    src/dialogue/serialize.cpp
    src/dialogue/system.cpp
    src/dialogue/validate.cpp
    src/dialogue/validate_refs.cpp
    src/engine.cpp
    src/input/input_sys.cpp
    src/item/item.cpp
    src/physics/collision.cpp
    src/physics/physics_sys.cpp
    src/physics/walkability.cpp
    src/quest/loader.cpp
    src/quest/registry.cpp
    src/quest/runner.cpp
    src/quest/serialize.cpp
    src/quest/system.cpp
    src/quest/validate.cpp
    src/render/render_sys.cpp
    src/sprites/atlas_clips.cpp
    src/sprites/atlas_clips_loader.cpp
    src/sprites/atlas_clips_serializer.cpp
    src/sprites/character_registry.cpp
    src/sprites/character_sheet_loader.cpp
    src/sprites/character_sheet_serializer.cpp
    src/sprites/sprite_atlas_loader.cpp
    src/sprites/sprite_sheet_clips_loader.cpp
    src/sprites/sprite_sheet_clips_serializer.cpp
    src/ui/dialog_box.cpp
    src/ui/inventory_panel.cpp
    src/ui/nine_patch.cpp
    src/ui/prompt_box.cpp
    src/ui/ui_draw.cpp
    src/world/actors/actor_loader.cpp
    src/world/camera_system.cpp
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

# engine_factory.cpp references platform::create_platform in
# engine_platform_glfw. In a Unity batch the linker can't skip it; keep it
# standalone so the linker only pulls it in when make_engine() is actually
# called.
list(APPEND ENGINE_SOURCES src/engine_factory.cpp)

add_library(engine STATIC ${ENGINE_SOURCES})

# Public headers are in the 'include' folder.
target_include_directories(engine PUBLIC include)

# C++23 is PUBLIC so it propagates to consumers; the warning policy is applied
# explicitly per-target (PRIVATE flags, never inherited through links).
corundum_use_cxx23(engine)
corundum_enable_warnings(engine)

# Link dependencies that the engine itself needs
target_link_libraries(engine PUBLIC nlohmann_json::nlohmann_json
                                   PUBLIC nlohmann_json_schema_validator::validator)

# PCH — stable STL + third-party headers that are slow to parse
target_precompile_headers(engine PRIVATE src/pch.hpp)

# engine_factory.cpp must stay outside Unity batches (see above).
set_source_files_properties(src/engine_factory.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)

# Add the platform sub-module
add_subdirectory(src/platform)
