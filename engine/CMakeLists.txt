# Define the core engine library
add_library(engine STATIC
    src/engine.cpp
    src/resources/character_registry.cpp
    src/gameplay/dialogue/action.cpp
    src/gameplay/dialogue/expr.cpp
    src/gameplay/dialogue/loader.cpp
    src/gameplay/dialogue/query.cpp
    src/gameplay/dialogue/registry.cpp
    src/gameplay/dialogue/system.cpp
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
)

# Public headers are in the 'include' folder.
target_include_directories(engine PUBLIC include)

# Link dependencies that the engine itself needs
target_link_libraries(engine PUBLIC nlohmann_json::nlohmann_json)

# Add the platform sub-module
add_subdirectory(src/platform)

target_compile_options(engine PRIVATE -Wall -Wextra -Wpedantic)
