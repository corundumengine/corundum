# Shared editor toolkit: compiles ImGui core + sokol_imgui and provides the
# ToolHost (RAII window/GpuContext/TextureCache + simgui lifecycle), reusable
# ImGui widgets, and generic editor-domain helpers used by tools/editors/*.
# Editors consume this via the corundum::toolkit ALIAS and never reach into
# the engine platform internals directly.

include(${PROJECT_SOURCE_DIR}/cmake/CompilerWarnings.cmake)

add_library(corundum_toolkit STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    src/host/simgui_impl.cpp
    src/host/tool_host.cpp
    src/host/tool_config.cpp
    src/widgets/ui_theme.cpp
    src/widgets/fonts.cpp
    src/widgets/file_browser.cpp
)

add_library(corundum::toolkit ALIAS corundum_toolkit)

target_include_directories(corundum_toolkit
    PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
        ${sokol_SOURCE_DIR}
        ${sokol_SOURCE_DIR}/util
        ${stb_SOURCE_DIR}
        include
)

target_link_libraries(corundum_toolkit
    PUBLIC engine_platform_glfw glfw corundum::platform_config
)

# ImGui's own headers are not warning-clean, so this target never enables -Werror.
target_compile_definitions(corundum_toolkit PUBLIC SOKOL_IMGUI_NO_SOKOL_APP)

corundum_use_cxx23(corundum_toolkit)

if(APPLE)
  set_source_files_properties(src/host/simgui_impl.cpp PROPERTIES LANGUAGE OBJCXX)
  target_link_libraries(corundum_toolkit PRIVATE "-framework Metal" "-framework QuartzCore")
endif()
