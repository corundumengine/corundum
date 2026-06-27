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

# ── ccache (auto-detect; transparent to the build) ──────────────────────────
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" CACHE FILEPATH "ccache launcher for C++")
  set(CMAKE_OBJCXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" CACHE FILEPATH "ccache launcher for Objective-C++")
endif()

set(FETCHCONTENT_QUIET FALSE)
include(FetchContent)

# ── Options for consumer projects ──────────────────────────────────────────────
option(CORUNDUM_BUILD_TOOLS "Build game developer tools" ON)
option(CORUNDUM_BUILD_TESTS "Build unit tests" ON)
option(CORUNDUM_UNITY_BUILD "Enable Unity/Jumbo builds for faster compilation" ON)

# ── Dependencies ───────────────────────────────────────────────────────────────
# Disable Unity builds for third-party code — external headers often lack
# include guards or mix C/ObjC/C++ sources that don't batch cleanly.
set(CMAKE_UNITY_BUILD OFF)

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
    GIT_TAG        v1.92.8
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
    GIT_TAG        VER-2-14-3
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(freetype)

FetchContent_Declare(
    sokol
    GIT_REPOSITORY https://github.com/floooh/sokol.git
    GIT_TAG        28f9d8d44d92dab8536791a9f7d13d7e911a2b39
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

# ── Engine and tools ───────────────────────────────────────────────────────────
# Unity/Jumbo builds for first-party code only.
if(CORUNDUM_UNITY_BUILD)
  set(CMAKE_UNITY_BUILD ON)
  set(CMAKE_UNITY_BUILD_BATCH_SIZE 8)
endif()

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

  # ── Static analysis ────────────────────────────────────────────────────────
  find_program(CLANG_TIDY clang-tidy)
  if(CLANG_TIDY)
    add_custom_target(run_tidy
          COMMAND ${CLANG_TIDY}
              -p ${CMAKE_BINARY_DIR}
              --warnings-as-errors='*'
              ${FORMAT_SOURCES}
          WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
          COMMENT "Running clang-tidy on all source files"
          COMMAND_EXPAND_LISTS
      )
  else()
    add_custom_target(run_tidy
          COMMAND ${CMAKE_COMMAND} -E echo
              "clang-tidy not found — install it to use this target"
      )
  endif()
endif()

# ── Tests ─────────────────────────────────────────────────────────────────────
include(CTest)
if(CORUNDUM_BUILD_TESTS)
  add_subdirectory(tests)
endif()
