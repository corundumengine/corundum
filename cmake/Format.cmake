# Developer convenience target: formatting (format_code). Guarded by the
# CORUNDUM_BUILD_* options; the root CMakeLists includes this file before
# Dependencies.cmake so a missing clang-format fails fast.
#
# clang-format is resolved by find_program with HINTS so the target works
# whether or not LLVM is on PATH (same search order as
# scripts/run_tidy.sh). clang-tidy itself lives in scripts/run_tidy.sh — it
# takes an explicit file list, so the per-file diagnostics you get from clangd
# in the editor stay the primary flow.

include_guard(GLOBAL)

# clang-format comes from the same LLVM install as the compiler, so resolve the
# prefix through the shared resolver and never re-derive it here.
include("${CMAKE_CURRENT_LIST_DIR}/ResolveLLVMPrefix.cmake")
corundum_resolve_llvm_prefix(_corundum_llvm_prefix)
set(LLVM_PREFIX "${_corundum_llvm_prefix}" CACHE PATH "LLVM install prefix")

find_program(LLVM_CLANG_FORMAT clang-format
    HINTS "${_corundum_llvm_prefix}/bin"
    REQUIRED
    DOC "clang-format from LLVM")

# Reformat every first-party source in place: `cmake --build --preset format`.
# Only built when at least one consumer (tools or tests) is enabled, matching
# the old root-level guard.
if(CORUNDUM_BUILD_TOOLS OR CORUNDUM_BUILD_TESTS)
  file(GLOB_RECURSE FORMAT_SOURCES
        "engine/src/**/*.cpp"
        "engine/include/corundum/**/*.hpp"
        "engine/tests/**/*.cpp"
        "tools/toolkit/**/*.cpp"
        "tools/toolkit/**/*.hpp"
        "tools/editors/**/*.cpp"
        "tools/editors/**/*.hpp"
        "tools/mapview/**/*.cpp"
        "tools/mapview/**/*.hpp"
        "tools/platform/**/*.cpp"
        "tools/platform/**/*.hpp"
        "tools/tests/*.cpp"
    )
  add_custom_target(format_code
        COMMAND ${LLVM_CLANG_FORMAT} -i ${FORMAT_SOURCES}
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        COMMENT "Running ${LLVM_CLANG_FORMAT} on all source files"
        VERBATIM
    )
endif()
