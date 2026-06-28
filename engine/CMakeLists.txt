# Define the core engine library
add_library(engine STATIC
    src/engine.cpp
    src/engine_factory.cpp
    src/resources/character_registry.cpp
    src/gameplay/dialogue/action.cpp
    src/gameplay/dialogue/expr.cpp
    src/gameplay/dialogue/loader.cpp
    src/gameplay/dialogue/query.cpp
    src/gameplay/dialogue/registry.cpp
    src/gameplay/dialogue/system.cpp
    src/gameplay/quest/loader.cpp
    src/gameplay/quest/registry.cpp
    src/gameplay/quest/system.cpp
    src/core/files.cpp
    src/physics/collision.cpp
    src/core/game_config.cpp
    src/gameplay/world/actors/actor_loader.cpp
    src/gameplay/world/portals/portal.cpp
    src/anim/sys/anim_sys.cpp
    src/gameplay/sys/camera_system.cpp
    src/gameplay/sys/dialogue_system.cpp
    src/input/sys/input_sys.cpp
    src/physics/sys/physics_sys.cpp
    src/render/sys/render_sys.cpp
    src/gameplay/world/tilemap/loader.cpp
    src/gameplay/world/tilemap/world_manifest.cpp
    src/gameplay/world/spawn.cpp
    src/gameplay/world/map_view.cpp
    src/gameplay/world/transition.cpp
    src/gameplay/world/update.cpp
    src/gameplay/ui/nine_patch.cpp
    src/gameplay/ui/dialog_box.cpp
    src/debug/debug_overlay.cpp
    src/audio/sys/audio_sys.cpp
)

# Public headers are in the 'include' folder.
target_include_directories(engine PUBLIC include)

# Link dependencies that the engine itself needs
target_link_libraries(engine PUBLIC nlohmann_json::nlohmann_json)

# PCH — stable STL + third-party headers that are slow to parse
target_precompile_headers(engine PRIVATE src/pch.hpp)

# engine_factory.cpp references platform::create_platform in engine_platform_glfw.
# In a Unity batch the linker can't skip it; keep it standalone so the linker only
# pulls it in when make_engine() is actually called.
set_source_files_properties(src/engine_factory.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)

# Add the platform sub-module
add_subdirectory(src/platform)

target_compile_options(engine PRIVATE -Wall -Wextra -Wpedantic)
