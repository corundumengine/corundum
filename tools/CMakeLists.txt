# Developer tools: the tilesmith/spritesmith/loom GUI editors and the
# headless mapview renderer. Each editor is a thin executable over
# corundum::toolkit; C++23 arrives transitively through the target links,
# while the warning policy is applied explicitly per-target below.

include(${PROJECT_SOURCE_DIR}/cmake/CompilerWarnings.cmake)

add_subdirectory(toolkit)

# ── Tilesmith Editor ────────────────────────────────────────────────────────────
add_executable(tilesmith
    editors/tilesmith/main.cpp
    editors/tilesmith/menu.cpp
    editors/tilesmith/new_map_dialog.cpp
    editors/tilesmith/tilemap_rendering.cpp
    editors/tilesmith/paint.cpp
    editors/tilesmith/elevation_paint.cpp
    editors/tilesmith/ramp_paint.cpp
    editors/tilesmith/fill.cpp
    editors/tilesmith/save.cpp
    editors/tilesmith/input.cpp
    editors/tilesmith/undo.cpp
    editors/tilesmith/render_canvas.cpp
    editors/tilesmith/render_layer_strip.cpp
    editors/tilesmith/render_tile_grid.cpp
    editors/tilesmith/render_status_bar.cpp
    editors/tilesmith/render_collision.cpp
    editors/tilesmith/render_elevation.cpp
    editors/tilesmith/render_portal.cpp
    editors/tilesmith/render_walkability.cpp
    editors/tilesmith/render_ramps.cpp
)

target_include_directories(tilesmith
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/editors/tilesmith
)

target_link_libraries(tilesmith
    PRIVATE
        corundum::toolkit
)

corundum_enable_warnings(tilesmith)
corundum_limit_stack_frames(tilesmith)

# ── Spritesmith Editor ──────────────────────────────────────────────────────────
add_executable(spritesmith
    editors/spritesmith/main.cpp
    editors/spritesmith/load.cpp
    editors/spritesmith/save.cpp
    editors/spritesmith/input.cpp
    editors/spritesmith/render_canvas.cpp
    editors/spritesmith/render_side_panel.cpp
    editors/spritesmith/render_anim_preview.cpp
    editors/spritesmith/render_preview_panel.cpp
    editors/spritesmith/render_status_bar.cpp
)

target_include_directories(spritesmith
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/editors/spritesmith
)

target_link_libraries(spritesmith
    PRIVATE
        corundum::toolkit
)

corundum_enable_warnings(spritesmith)
corundum_limit_stack_frames(spritesmith)

# ── Loom Editor ─────────────────────────────────────────────────────────────────
add_executable(loom
    editors/loom/main.cpp
    editors/loom/actions.cpp
    editors/loom/file_io.cpp
    editors/loom/graph_layout.cpp
    editors/loom/render_graph.cpp
    editors/loom/render_inspector.cpp
    editors/loom/render_item_editor.cpp
    editors/loom/render_keys_panel.cpp
    editors/loom/render_node_list.cpp
    editors/loom/render_quest_editor.cpp
)

target_include_directories(loom
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/editors/loom
)

target_link_libraries(loom
    PRIVATE
        corundum::toolkit
)

corundum_enable_warnings(loom)
corundum_limit_stack_frames(loom)

# ── Mapview Tool (headless) ─────────────────────────────────────────────────────
# A CLI that renders a top-down overview PNG of a chunk-based world from its tilemaps.
# Links only the engine's parsing library (engine) + stb headers — no GLFW/sokol.
#
# mapview_main.cpp defines STB_IMAGE_IMPLEMENTATION itself, so this executable must
# never link engine_platform_glfw (which compiles the same implementation in
# glfw/stb_impl.cpp) — that would be a duplicate-symbol link error.
add_executable(mapview
    mapview/mapview_main.cpp
)

# stb is third-party: SYSTEM keeps its warnings and clang-tidy findings out of this
# target, which builds under -Werror (see engine/src/platform/CMakeLists.txt).
target_include_directories(mapview
    SYSTEM PRIVATE
        ${stb_SOURCE_DIR}
)

target_link_libraries(mapview
    PRIVATE
        engine
)

corundum_use_cxx23(mapview)
corundum_enable_warnings(mapview)
corundum_limit_stack_frames(mapview)
