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
if(APPLE)
  # sokol_gfx.h uses #import on macOS; the implementation and renderer files
  # that include it must be compiled as Objective-C++.
  list(APPEND PLATFORM_GLFW_SOURCES glfw/glfw_window_metal.mm)
endif()

add_library(engine_platform_glfw STATIC ${PLATFORM_GLFW_SOURCES})

target_include_directories(engine_platform_glfw
    PRIVATE
        ${sokol_SOURCE_DIR}
        ${stb_SOURCE_DIR}
)

target_link_libraries(engine_platform_glfw
    PUBLIC  engine
    PRIVATE glfw freetype
)

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
  target_compile_definitions(engine_platform_glfw PRIVATE SOKOL_METAL)
  target_link_libraries(engine_platform_glfw
        PRIVATE "-framework Metal" "-framework QuartzCore" "-framework AudioToolbox" "-framework CoreAudio"
    )
else()
  target_compile_definitions(engine_platform_glfw PRIVATE SOKOL_GLCORE)
  find_package(OpenGL REQUIRED)
  target_link_libraries(engine_platform_glfw PRIVATE OpenGL::GL)
endif()

corundum_use_cxx23(engine_platform_glfw)
corundum_enable_warnings(engine_platform_glfw)

add_library(corundum::engine ALIAS engine_platform_glfw)

# sokol_gfx.h lacks include guards — double-inclusion in a Unity batch
# produces redefinition errors. Keep this target out of Unity builds.
set_target_properties(engine_platform_glfw PROPERTIES UNITY_BUILD OFF)

# ── Null backend (headless) ────────────────────────────────────────────────────
# Header-only null platform backend (Window/GpuContext/Renderer no-ops) for
# headless lifecycle tests in tests/test_engine_lifecycle.cpp. Has no
# GLFW/sokol/FreeType dependency; consumers link `corundum::engine_platform_null`
# (or just `engine_platform_null`) in addition to `engine`.
add_library(engine_platform_null STATIC
    null/null_stub.cpp
)
target_link_libraries(engine_platform_null
    PUBLIC  engine
)
corundum_use_cxx23(engine_platform_null)
corundum_enable_warnings(engine_platform_null)
