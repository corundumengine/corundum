# Platform backends: GLFW + sokol (windowed) and null (headless tests).
#
# `engine_platform_glfw` is the real backend. Consumers use the
# `corundum::engine` ALIAS so switching to the null backend for headless runs
# is a one-line change.
#
# Platform files may include engine headers (never the reverse) — engine/ must
# not include from here. See CLAUDE.md "Layer discipline".

include(${PROJECT_SOURCE_DIR}/cmake/CompilerWarnings.cmake)

# ── GLFW + sokol backend ───────────────────────────────────────────────────────
# ObjC++ files are added separately (Apple only).
set(PLATFORM_GLFW_SOURCES
    glfw/glfw_window.cpp
    glfw/gpu_context.cpp
    glfw/platform_factory.cpp
    glfw/sokol_renderer.cpp
    glfw/stb_impl.cpp
    glfw/font_atlas.cpp
    glfw/input_translator.cpp
    glfw/texture_cache.cpp
    sokol/sokol_impl.cpp
    sokol/sokol_audio_impl.cpp
    sokol/sokol_audio_backend.cpp
)
# The graphics-API seam is a build-time choice: exactly one of these translation units links, so
# the shared window code carries no Metal/GLCore conditional.
if(APPLE)
  # sokol_gfx.h uses #import on macOS; the implementation and renderer files
  # that include it must be compiled as Objective-C++.
  list(APPEND PLATFORM_GLFW_SOURCES
        glfw/metal/glfw_window_metal.mm
        glfw/metal/gpu_backend.mm
        glfw/metal/render_backend.mm
        glfw/metal/window_graphics.mm)
else()
  list(APPEND PLATFORM_GLFW_SOURCES
        glfw/glcore/gpu_backend.cpp
        glfw/glcore/render_backend.cpp
        glfw/glcore/window_graphics.cpp)
endif()

add_library(engine_platform_glfw STATIC ${PLATFORM_GLFW_SOURCES})

# sokol_gfx.h lacks include guards, so it cannot be batched into a unity translation
# unit. Opt out even when a parent project enables unity builds globally.
set_property(TARGET engine_platform_glfw PROPERTY UNITY_BUILD OFF)

# stb is third-party: SYSTEM keeps its warnings and clang-tidy findings out of
# this target, which builds under the project's warnings-as-errors policy
# (CMAKE_COMPILE_WARNING_AS_ERROR in the base configure preset).
target_include_directories(engine_platform_glfw
    PRIVATE
        ${sokol_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/glfw
)
target_include_directories(engine_platform_glfw
    SYSTEM PRIVATE
        ${stb_SOURCE_DIR}
)

target_link_libraries(engine_platform_glfw
    PUBLIC  engine
    PRIVATE glfw freetype
)

# The SOKOL_* graphics define is owned by cmake/Platform.cmake, shared with the
# toolkit so both compile against the same API.
target_link_libraries(engine_platform_glfw PRIVATE corundum::platform_config)

# FreeType may bring in harfbuzz as a transitive dep without exposing it in
# its INTERFACE_LINK_LIBRARIES. Link it explicitly so symbols resolve.
if(TARGET harfbuzz)
  target_link_libraries(engine_platform_glfw PRIVATE harfbuzz)
endif()

if(APPLE)
  set_source_files_properties(
        sokol/sokol_impl.cpp
        glfw/gpu_context.cpp
        glfw/sokol_renderer.cpp
        sokol/sokol_audio_impl.cpp
        sokol/sokol_audio_backend.cpp
        glfw/texture_cache.cpp
        PROPERTIES LANGUAGE OBJCXX
    )
  target_link_libraries(engine_platform_glfw
        PRIVATE "-framework Metal" "-framework QuartzCore" "-framework AudioToolbox" "-framework CoreAudio"
    )
else()
  find_package(OpenGL REQUIRED)
  target_link_libraries(engine_platform_glfw PRIVATE OpenGL::GL)

  if(UNIX)
    # sokol_audio's Linux/BSD backend talks to ALSA directly. Like OpenGL it is
    # a system library with nothing to fetch, so it is found rather than
    # vendored; without the link, sokol_audio_impl.cpp compiles but the final
    # executable has undefined snd_* references.
    find_package(ALSA REQUIRED)
    target_link_libraries(engine_platform_glfw PRIVATE ALSA::ALSA)
  endif()
endif()

corundum_use_cxx23(engine_platform_glfw)
corundum_enable_warnings(engine_platform_glfw)
corundum_limit_stack_frames(engine_platform_glfw)

add_library(corundum::engine ALIAS engine_platform_glfw)

# ── Null backend (headless) ────────────────────────────────────────────────────
# Header-only null platform backend (Window/GpuContext/Renderer no-ops) for
# headless lifecycle tests in engine/tests/root/test_engine_lifecycle.cpp. Has no
# GLFW/sokol/FreeType dependency; consumers link `engine_platform_null` in
# addition to `engine`.
add_library(engine_platform_null INTERFACE)
target_link_libraries(engine_platform_null INTERFACE engine)
