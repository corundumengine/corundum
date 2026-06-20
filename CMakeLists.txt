cmake_minimum_required(VERSION 3.28)
project(
  CorundumEngine
  VERSION 0.1.0
  LANGUAGES CXX OBJCXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(FETCHCONTENT_QUIET FALSE)
include(FetchContent)

# ── Options for consumer projects ──────────────────────────────────────────────
option(CORUNDUM_BUILD_TOOLS "Build game developer tools" ON)
option(CORUNDUM_BUILD_TESTS "Build unit tests" ON)

# ── Dependencies ───────────────────────────────────────────────────────────────

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.12.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest
    GIT_TAG        v2.5.2
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(doctest)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(imgui)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
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
    GIT_TAG        VER-2-13-3
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(freetype)

FetchContent_Declare(
    sokol
    GIT_REPOSITORY https://github.com/floooh/sokol.git
    GIT_TAG        ae0bc31daad8a60457cad4b5dae9223f237b2e34
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(sokol)

FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(stb)

# ── Engine and tools ───────────────────────────────────────────────────────────
add_subdirectory(engine)
if(CORUNDUM_BUILD_TOOLS)
  add_subdirectory(tools)
endif()

# ── Formatting ─────────────────────────────────────────────────────────────────
if(CORUNDUM_BUILD_TOOLS OR CORUNDUM_BUILD_TESTS)
  file(GLOB_RECURSE FORMAT_SOURCES
        "engine/src/**/*.cpp"
        "engine/include/corundum/**/*.hpp"
        "tests/*.cpp"
        "tools/editors/**/*.cpp"
        "tools/editors/**/*.hpp"
        "tools/platform/**/*.cpp"
        "tools/platform/**/*.hpp"
    )
  add_custom_target(format_code
        COMMAND clang-format -i ${FORMAT_SOURCES}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running code formatter on all source files"
        VERBATIM
    )
endif()

# ── Tests ─────────────────────────────────────────────────────────────────────
include(CTest)
if(CORUNDUM_BUILD_TESTS)
  add_subdirectory(tests)
endif()
