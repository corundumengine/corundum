# ── Shared ImGui + sokol_imgui backend ─────────────────────────────────────────
# Compiles ImGui core, the GLFW input backend, and the sokol_imgui render backend
# into a single static library that both tools link against.

add_library(tools_imgui_backend STATIC
    # ImGui core
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    # GLFW input backend
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    # sokol_imgui rendering backend (one SOKOL_IMGUI_IMPL per link unit)
    platform/simgui_impl.cpp
    # ToolApp window + frame lifecycle
    platform/tool_app.cpp
)

target_include_directories(tools_imgui_backend
    PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
        ${sokol_SOURCE_DIR}
        ${sokol_SOURCE_DIR}/util
        ${stb_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/platform
)

target_link_libraries(tools_imgui_backend
    PUBLIC  engine_platform_glfw glfw
)

# sokol_imgui.h is used without sokol_app.h (GLFW handles windowing instead).
target_compile_definitions(tools_imgui_backend PUBLIC SOKOL_IMGUI_NO_SOKOL_APP)

# ImGui sources are not warning-clean; exempt from warnings-as-errors
target_compile_options(tools_imgui_backend PRIVATE -Wno-all -Wno-extra)

if(APPLE)
  set_source_files_properties(platform/simgui_impl.cpp PROPERTIES LANGUAGE OBJCXX)
  target_compile_definitions(tools_imgui_backend PRIVATE SOKOL_METAL)
  target_link_libraries(tools_imgui_backend PRIVATE "-framework Metal" "-framework QuartzCore")
elseif(WIN32)
  target_compile_definitions(tools_imgui_backend PRIVATE SOKOL_D3D11)
else()
  target_compile_definitions(tools_imgui_backend PRIVATE SOKOL_GLCORE)
endif()

# ── Common tools library ────────────────────────────────────────────────────────
add_library(tools_common STATIC
    editors/common/tool_texture.cpp
    editors/common/ui_theme.cpp
)

target_include_directories(tools_common
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/editors
)

target_link_libraries(tools_common
    PUBLIC
        tools_imgui_backend
        nlohmann_json::nlohmann_json
)

target_compile_options(tools_common PRIVATE -Wall -Wextra -Wpedantic)

# ── Tilesmith Editor ────────────────────────────────────────────────────────────
add_executable(tilesmith
    editors/tilesmith/main.cpp
    editors/tilesmith/new_map_dialog.cpp
    editors/tilesmith/tilemap_rendering.cpp
    editors/tilesmith/paint.cpp
    editors/tilesmith/elevation_paint.cpp
    editors/tilesmith/save.cpp
    editors/tilesmith/input.cpp
    editors/tilesmith/render_canvas.cpp
    editors/tilesmith/render_layer_strip.cpp
    editors/tilesmith/render_tile_grid.cpp
    editors/tilesmith/render_status_bar.cpp
    editors/tilesmith/render_collision.cpp
    editors/tilesmith/render_elevation.cpp
    editors/tilesmith/render_portal.cpp
)

target_include_directories(tilesmith
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/editors/tilesmith
)

target_link_libraries(tilesmith
    PRIVATE
        tools_common
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
        tools_common
)

target_compile_options(spritesmith PRIVATE -Wall -Wextra -Wpedantic)

# Anonymous namespace helpers (sheet_cols, sheet_rows) collide with Unity builds
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
        tools_common
)

target_compile_options(talesmith PRIVATE -Wall -Wextra -Wpedantic)
