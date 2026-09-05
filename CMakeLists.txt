cmake_minimum_required(VERSION 3.28)
project(
  CorundumEngine
  VERSION 0.1.0
  LANGUAGES CXX OBJCXX
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ccache (auto-detect; transparent to the build)
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" CACHE FILEPATH "ccache launcher for C++")
  set(CMAKE_OBJCXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" CACHE FILEPATH "ccache launcher for Objective-C++")
endif()

# Options for consumer projects
option(CORUNDUM_BUILD_TOOL_HOST "Build the ImGui+sokol host library for editor tools" ON)
option(CORUNDUM_BUILD_TOOLS "Build game developer tools" ON)
option(CORUNDUM_BUILD_TESTS "Build unit tests" ON)
option(CORUNDUM_BUILD_DOCS "Build Doxygen documentation" OFF)
option(CORUNDUM_UNITY_BUILD "Enable Unity/Jumbo builds for faster compilation" ON)

# Developer targets (format_code, build_docs). Included before Dependencies.cmake
# so the REQUIRED clang-format check fails fast instead of after dependency fetches.
include(cmake/FormatAndDocs.cmake)
include(cmake/Dependencies.cmake)

# First-party targets
# Unity/Jumbo builds for first-party code only (third-party is excluded in
# Dependencies.cmake). Targets that don't batch cleanly opt out individually.
if(CORUNDUM_UNITY_BUILD)
  set(CMAKE_UNITY_BUILD ON)
  set(CMAKE_UNITY_BUILD_BATCH_SIZE 8)
endif()

add_subdirectory(engine)
if(CORUNDUM_BUILD_TOOL_HOST)
  add_subdirectory(tool_host)
endif()
if(CORUNDUM_BUILD_TOOLS)
  add_subdirectory(tools)
endif()

include(CTest)
if(CORUNDUM_BUILD_TESTS)
  add_subdirectory(tests)
endif()
