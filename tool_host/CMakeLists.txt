# Shared editor host library: compiles ImGui core + sokol_imgui and provides
# the ToolHost (RAII window/GpuContext/TextureCache + simgui lifecycle).
# Tools consume this via the corundum::tool_host ALIAS and never reach into
# the engine platform internals directly.

include(${PROJECT_SOURCE_DIR}/cmake/CompilerWarnings.cmake)

add_library(corundum_tool_host STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    simgui_impl.cpp
    src/tool_host.cpp
    src/tool_config.cpp
    src/ui_theme.cpp
    src/fonts.cpp
    src/file_browser.cpp
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

# ImGui's own headers are not warning-clean, so this target never enables -Werror.
target_compile_definitions(corundum_tool_host PUBLIC SOKOL_IMGUI_NO_SOKOL_APP)

corundum_use_cxx23(corundum_tool_host)

if(APPLE)
  set_source_files_properties(simgui_impl.cpp PROPERTIES LANGUAGE OBJCXX)
  target_compile_definitions(corundum_tool_host PUBLIC SOKOL_METAL)
  target_link_libraries(corundum_tool_host PRIVATE "-framework Metal" "-framework QuartzCore")
elseif(WIN32)
  target_compile_definitions(corundum_tool_host PUBLIC SOKOL_D3D11)
else()
  target_compile_definitions(corundum_tool_host PUBLIC SOKOL_GLCORE)
endif()

set_target_properties(corundum_tool_host PROPERTIES UNITY_BUILD OFF)
