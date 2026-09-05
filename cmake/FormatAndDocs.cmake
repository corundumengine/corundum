# Developer convenience targets: formatting (format_code) and docs (build_docs).
# Guarded by the CORUNDUM_BUILD_* options; the root CMakeLists includes this
# file before Dependencies.cmake so a missing clang-format fails fast.
#
# clang-format is resolved by find_program with HINTS so the target works
# whether or not Homebrew LLVM is on PATH (same search order as
# scripts/run_tidy.sh). clang-tidy itself lives in scripts/run_tidy.sh — it
# takes an explicit file list, so the per-file diagnostics you get from clangd
# in the editor stay the primary flow.

include_guard(GLOBAL)

# build_docs uses FetchContent to pull doxygen-awesome-css. Include it here so
# the declare works even though this file is processed before Dependencies.cmake
# (idempotent — Dependencies.cmake includes it again later).
include(FetchContent)

# Resolve LLVM_PREFIX: explicit cache variable → $LLVM_PREFIX env → Homebrew
# prefixes. (The compiler itself is pinned in CMakePresets.json via
# cmake/llvm-clang.cmake; this only locates the bundled clang-format/doxygen.)
if(NOT DEFINED LLVM_PREFIX)
  if(DEFINED ENV{LLVM_PREFIX})
    set(LLVM_PREFIX "$ENV{LLVM_PREFIX}")
  elseif(EXISTS "/opt/homebrew/opt/llvm/bin/clang-format")
    set(LLVM_PREFIX "/opt/homebrew/opt/llvm")
  elseif(EXISTS "/usr/local/opt/llvm/bin/clang-format")
    set(LLVM_PREFIX "/usr/local/opt/llvm")
  else()
    set(LLVM_PREFIX "")
  endif()
endif()
set(LLVM_PREFIX "${LLVM_PREFIX}" CACHE PATH "LLVM install prefix")

find_program(LLVM_CLANG_FORMAT clang-format
    HINTS "${LLVM_PREFIX}/bin"
    REQUIRED
    DOC "clang-format from LLVM")

# ── Formatting ────────────────────────────────────────────────────────────────
# Reformat every first-party source in place: `cmake --build --preset format`.
# Only built when at least one consumer (tools or tests) is enabled, matching
# the old root-level guard.
if(CORUNDUM_BUILD_TOOLS OR CORUNDUM_BUILD_TESTS)
  file(GLOB_RECURSE FORMAT_SOURCES
        "engine/src/**/*.cpp"
        "engine/include/corundum/**/*.hpp"
        "tests/*.cpp"
        "tool_host/**/*.cpp"
        "tool_host/**/*.hpp"
        "tools/editors/**/*.cpp"
        "tools/editors/**/*.hpp"
        "tools/mapview/**/*.cpp"
        "tools/mapview/**/*.hpp"
        "tools/platform/**/*.cpp"
        "tools/platform/**/*.hpp"
    )
  add_custom_target(format_code
        COMMAND ${LLVM_CLANG_FORMAT} -i ${FORMAT_SOURCES}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running ${LLVM_CLANG_FORMAT} on all source files"
        VERBATIM
    )
endif()

# Documentation
if(CORUNDUM_BUILD_DOCS)
  find_program(DOXYGEN_EXECUTABLE doxygen)
  if(DOXYGEN_EXECUTABLE)
    FetchContent_Declare(
        doxygen_awesome_css
        GIT_REPOSITORY https://github.com/jothepro/doxygen-awesome-css.git
        GIT_TAG        v2.4.2
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE
    )
    FetchContent_MakeAvailable(doxygen_awesome_css)

    set(DOXYGEN_OUT ${CMAKE_BINARY_DIR}/Doxyfile)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Doxyfile.in ${DOXYGEN_OUT} @ONLY)
    add_custom_target(build_docs
          COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_OUT}
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
              ${CMAKE_CURRENT_SOURCE_DIR}/misc/logo.png
              ${CMAKE_BINARY_DIR}/docs/html/misc/logo.png
          WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
          COMMENT "Generating Doxygen API documentation for engine/"
          VERBATIM
      )
  else()
    add_custom_target(build_docs
          COMMAND ${CMAKE_COMMAND} -E echo
              "doxygen not found — install it to use this target"
      )
  endif()
endif()
