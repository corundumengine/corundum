# Third-party dependencies.
#
# Everything is fetched at configure time via FetchContent (no find_package,
# no manual installs). The only exceptions are OpenGL (GLCore) and the audio
# runtime libraries (threads, libdl, libm), all found and linked in
# engine/src/platform/CMakeLists.txt; doctest is gated by CORUNDUM_BUILD_TESTS.
#
# FETCHCONTENT_QUIET is forced to FALSE so dependency configure logs stay
# visible — nothing here is skippable via -DCORUNDUM_FETCHCONTENT=OFF.

include_guard(GLOBAL)

set(FETCHCONTENT_QUIET FALSE)
include(FetchContent)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.12.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

FetchContent_Declare(
    nlohmann_json_schema_validator
    GIT_REPOSITORY https://github.com/pboettch/json-schema-validator.git
    GIT_TAG        2.4.0
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(nlohmann_json_schema_validator)

if(CORUNDUM_BUILD_TESTS)
  # Test-only dependency: skip the fetch entirely when tests are not built.
  FetchContent_Declare(
        doctest
        GIT_REPOSITORY https://github.com/doctest/doctest
        GIT_TAG        v2.5.3
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE
    )
  FetchContent_MakeAvailable(doctest)

  # MSVC forward-declares std::basic_ostream, so doctest's generic stringifier
  # cannot stream a std::string_view there; force the real standard headers for
  # every test target (one place, so a new target can't miss it).
  target_compile_definitions(doctest INTERFACE DOCTEST_CONFIG_USE_STD_HEADERS)
endif()

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.92.9b
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(imgui)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.5.1
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

FetchContent_Declare(
    freetype
    GIT_REPOSITORY https://gitlab.freedesktop.org/freetype/freetype.git
    GIT_TAG        VER-2-14-3
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(freetype)

# GLFW and FreeType ship doc comments that clang's -Wdocumentation flags. Mark
# the targets SYSTEM so their include directories are warning-suppressed in
# consumers instead of fixing third-party headers we do not own.
set_target_properties(glfw freetype PROPERTIES SYSTEM TRUE)

# SDL_GameControllerDB: the community gamepad mapping database. GLFW's built-in table is a subset
# that lacks common pads (the 8BitDo Pro 2 on macOS), so the GLFW backend embeds this file's rows
# for the build platform and loads them at startup (engine/src/platform/CMakeLists.txt). Data
# only: the repository has no CMakeLists.txt, so MakeAvailable just populates the source tree.
FetchContent_Declare(
    sdl_gamecontrollerdb
    GIT_REPOSITORY https://github.com/mdqinc/SDL_GameControllerDB.git
    GIT_TAG        c1d5289a1f713b30a2c121e9fe6529d39360be0f # master on 18-Oct-2026
    GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(sdl_gamecontrollerdb)

FetchContent_Declare(
    sokol
    GIT_REPOSITORY https://github.com/floooh/sokol.git
    GIT_TAG        2e75443dbd4940b5aa8d76a8e479f8e4b270b9a3 # 14-Sep-2026
    GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(sokol)

FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        31c1ad37456438565541f4919958214b6e762fb4
    GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(stb)

FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG        0.11.25
    GIT_SHALLOW    TRUE
    # Header-only use: a SOURCE_SUBDIR with no CMakeLists.txt makes MakeAvailable only populate the
    # source tree, never configure or build miniaudio's own examples and tests.
    SOURCE_SUBDIR  header-only
)
FetchContent_MakeAvailable(miniaudio)
