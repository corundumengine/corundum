# Single source of truth for the graphics API the sokol-based backends compile
# against. `engine_platform_glfw` and `corundum_toolkit` both link
# `corundum::platform_config`, so the sokol_gfx implementation, the sokol_imgui
# backend and the ImGui GLFW init path can never disagree about which API is in
# use (the bug that had the toolkit on D3D11 while sokol_gfx built for GLCore).
#
# Backend per platform: Metal on macOS, GLCore on Windows and Linux. GLCore is
# the least-churn choice because the toolkit's ImGui backend is OpenGL; see
# AGENTS.md "Architecture".

include_guard(GLOBAL)

add_library(corundum_platform_config INTERFACE)
add_library(corundum::platform_config ALIAS corundum_platform_config)

if(APPLE)
  target_compile_definitions(corundum_platform_config INTERFACE SOKOL_METAL)
else()
  target_compile_definitions(corundum_platform_config INTERFACE SOKOL_GLCORE)
endif()
