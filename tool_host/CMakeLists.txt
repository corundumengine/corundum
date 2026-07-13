# ── corundum_tool_host: ImGui + sokol_imgui host library ──────────────────
# Compiles ImGui core, the GLFW input backend, the sokol_imgui render
# backend, and the ToolHost class into a single static library. Owns the
# imgui/sokol/stb include dirs as PUBLIC usage requirements so external
# tool repos can consume this target via FetchContent without repeating the
# include-path plumbing.

add_library(corundum_tool_host STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    simgui_impl.cpp
    src/tool_host.cpp
)

add_library(corundum::tool_host ALIAS corundum_tool_host)

target_include_directories(corundum_tool_host
    PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
        ${sokol_SOURCE_DIR}
        ${sokol_SOURCE_DIR}/util
        ${stb_SOURCE_DIR}
        include
)

target_link_libraries(corundum_tool_host
    PUBLIC engine_platform_glfw glfw
)

target_compile_definitions(corundum_tool_host PUBLIC SOKOL_IMGUI_NO_SOKOL_APP)

# ImGui sources are not warning-clean; exempt from warnings-as-errors
target_compile_options(corundum_tool_host PRIVATE -Wno-all -Wno-extra)

if(APPLE)
  set_source_files_properties(simgui_impl.cpp PROPERTIES LANGUAGE OBJCXX)
  target_compile_definitions(corundum_tool_host PUBLIC SOKOL_METAL)
  target_link_libraries(corundum_tool_host PRIVATE "-framework Metal" "-framework QuartzCore")
elseif(WIN32)
  target_compile_definitions(corundum_tool_host PUBLIC SOKOL_D3D11)
else()
  target_compile_definitions(corundum_tool_host PUBLIC SOKOL_GLCORE)
endif()

# sokol_gfx.h lacks include guards — double-inclusion in a Unity batch
# produces redefinition errors.
set_target_properties(corundum_tool_host PROPERTIES UNITY_BUILD OFF)
