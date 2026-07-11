add_library(engine_platform_glfw STATIC
    glfw/glfw_window.cpp
    glfw/platform_factory.cpp
    sokol/sokol_impl.cpp
    glfw/sokol_renderer.cpp
    sokol/sokol_audio_impl.cpp
    sokol/sokol_audio_backend.cpp
    glfw/font_atlas.cpp
    glfw/input_translator.cpp
)

if(APPLE)
  target_sources(engine_platform_glfw PRIVATE glfw/glfw_window_metal.mm)
endif()

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
  # sokol_gfx.h uses #import on macOS; the implementation and renderer files
  # that include it must be compiled as Objective-C++.
  set_source_files_properties(
        sokol/sokol_impl.cpp
        glfw/sokol_renderer.cpp
        sokol/sokol_audio_impl.cpp
        sokol/sokol_audio_backend.cpp
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

target_compile_options(engine_platform_glfw PRIVATE -Wall -Wextra -Wpedantic)

# sokol_gfx.h lacks include guards — double-inclusion in a Unity batch
# produces redefinition errors. Keep this target out of Unity builds.
set_target_properties(engine_platform_glfw PROPERTIES UNITY_BUILD OFF)
