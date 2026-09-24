cmake_minimum_required(VERSION 3.28)
project(
  CorundumEngine
  VERSION 0.1.0
  LANGUAGES CXX OBJCXX
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Force unity builds off even when a parent project enables them globally: several
# first-party targets don't batch cleanly (sokol_gfx.h lacks include guards, test
# helpers share anonymous-namespace names, engine_factory must stay its own object).
set(CMAKE_UNITY_BUILD OFF)

# Options for consumer projects
option(CORUNDUM_BUILD_TOOLS "Build game developer tools" ON)
option(CORUNDUM_BUILD_TESTS "Build unit tests" ON)
option(CORUNDUM_BUILD_DOCS "Build Doxygen documentation" OFF)

# Developer targets (format_code, build_docs). Included before Dependencies.cmake
# so the REQUIRED clang-format check fails fast instead of after dependency fetches.
include(cmake/FormatAndDocs.cmake)
include(cmake/Dependencies.cmake)

# First-party targets
add_subdirectory(engine)
if(CORUNDUM_BUILD_TOOLS)
  add_subdirectory(tools)
endif()

include(CTest)
if(CORUNDUM_BUILD_TESTS)
  add_subdirectory(tests)
endif()
