# ── Tilesmith Editor ────────────────────────────────────────────────────────────
add_executable(tilesmith
    editors/tilesmith/main.cpp
    editors/tilesmith/new_map_dialog.cpp
    editors/tilesmith/tilemap_rendering.cpp
    editors/tilesmith/paint.cpp
    editors/tilesmith/elevation_paint.cpp
    editors/tilesmith/ramp_paint.cpp
    editors/tilesmith/fill.cpp
    editors/tilesmith/save.cpp
    editors/tilesmith/input.cpp
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
        corundum::tool_host
)

target_compile_options(tilesmith PRIVATE -Wall -Wextra -Wpedantic)

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
        corundum::tool_host
)

target_compile_options(spritesmith PRIVATE -Wall -Wextra -Wpedantic)

set_target_properties(spritesmith PROPERTIES UNITY_BUILD OFF)

# ── Talesmith Editor ─────────────────────────────────────────────────────────────
add_executable(talesmith
    editors/talesmith/main.cpp
    editors/talesmith/file_io.cpp
    editors/talesmith/graph_layout.cpp
    editors/talesmith/render_graph.cpp
    editors/talesmith/render_inspector.cpp
    editors/talesmith/render_node_list.cpp
    editors/talesmith/render_quest_editor.cpp
)

target_include_directories(talesmith
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/editors/talesmith
)

target_link_libraries(talesmith
    PRIVATE
        corundum::tool_host
)

target_compile_options(talesmith PRIVATE -Wall -Wextra -Wpedantic)
