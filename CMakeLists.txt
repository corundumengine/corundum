cmake_minimum_required(VERSION 3.28)
project(
  CorundumEngine
  VERSION 0.2.0
  LANGUAGES CXX
)

if(APPLE)
  enable_language(OBJCXX)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Force unity builds off even when a parent project enables them globally: several
# first-party targets don't batch cleanly (sokol_gfx.h lacks include guards, test
# helpers share anonymous-namespace names, engine_factory must stay its own object).
set(CMAKE_UNITY_BUILD OFF)

# Options for consumer projects
option(CORUNDUM_BUILD_TOOLS "Build game developer tools" ON)
option(CORUNDUM_BUILD_TESTS "Build unit tests" ON)
option(CORUNDUM_BUILD_DOCS "Build Doxygen documentation" OFF)

# Fail fast on a toolchain that can't build the engine's C++23 library surface,
# before any developer target or dependency fetch runs.
include(cmake/ToolchainChecks.cmake)

# Developer targets (format_code, build_docs). Included before Dependencies.cmake
# so the REQUIRED clang-format check fails fast instead of after dependency fetches.
include(cmake/FormatAndDocs.cmake)
include(cmake/Dependencies.cmake)

# Graphics-API selection (the sokol define) — one source of truth shared by the
# engine backend and the toolkit. Must exist before the first-party targets.
include(cmake/Platform.cmake)

# First-party targets
add_subdirectory(engine)
if(CORUNDUM_BUILD_TOOLS)
  add_subdirectory(tools)
endif()

include(CTest)
if(CORUNDUM_BUILD_TESTS)
  add_subdirectory(engine/tests)
  # tools/tests reads tilesmith's coords.hpp directly; gate it the same way engine/tests gates
  # its internal-header tests, so a vendored/tools-less checkout skips it cleanly at configure
  # time instead of hard-failing (today it would fail later, at compile time, the same way).
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tools/editors/tilesmith/coords.hpp)
    add_subdirectory(tools/tests)
  else()
    message(STATUS
            "tools tests skipped — tools/editors/tilesmith headers not reachable from ${CMAKE_CURRENT_SOURCE_DIR}")
  endif()
endif()
