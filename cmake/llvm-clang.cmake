# Pinned clang toolchain for every corundum build. This is an SDK descriptor,
# not a convenience finder: it selects the compiler, forces one C++ standard
# library, pins the linker, and fails configure loudly when any is unavailable.
# There is no silent fallback to an arbitrary system compiler.
#
# Prefix resolution lives in ResolveLLVMPrefix.cmake (shared with clang-format
# and run_tidy.sh): -DLLVM_PREFIX > $LLVM_PREFIX > Homebrew > /usr/lib/llvm-*.
#
# Linux note: clang defaults to the host libstdc++, which lacks corundum's
# C++23 library surface (<mdspan>, <flat_map>, <print>) before GCC 15. libc++ is
# forced on every platform so macOS and Linux compile the same standard library;
# install it with `apt install libc++-21-dev libc++abi-21-dev`.
#
# ToolchainChecks.cmake verifies that surface after project().
if(DEFINED _CORUNDUM_LLVM_CLANG_CMAKE_LOADED)
  return()
endif()
set(_CORUNDUM_LLVM_CLANG_CMAKE_LOADED TRUE)

include("${CMAKE_CURRENT_LIST_DIR}/ResolveLLVMPrefix.cmake")

corundum_resolve_llvm_prefix(_corundum_llvm_prefix)
if(_corundum_llvm_prefix)
  set(_corundum_llvm_hints "${_corundum_llvm_prefix}")
  # A resolved prefix is authoritative: a wrong explicit LLVM_PREFIX must fail,
  # not silently select a different clang from PATH.
  set(_corundum_find_restrict NO_DEFAULT_PATH)
  message(STATUS "corundum: LLVM prefix ${_corundum_llvm_prefix}")
else()
  set(_corundum_llvm_hints "")
  set(_corundum_find_restrict "")
endif()

# Toolchain files run before project(), so CMake's toolchain-dependent
# variables (CMAKE_EXECUTABLE_SUFFIX) don't exist yet.
set(_corundum_exe_suffix "")
if(CMAKE_HOST_WIN32)
  set(_corundum_exe_suffix ".exe")
endif()

find_program(CORUNDUM_CLANGXX
    NAMES clang++${_corundum_exe_suffix}
    HINTS ${_corundum_llvm_hints}
    PATH_SUFFIXES bin
    ${_corundum_find_restrict}
    REQUIRED
    DOC "clang++ used for every corundum build")
find_program(CORUNDUM_CLANG
    NAMES clang${_corundum_exe_suffix}
    HINTS ${_corundum_llvm_hints}
    PATH_SUFFIXES bin
    ${_corundum_find_restrict}
    REQUIRED
    DOC "clang used for every corundum build")

# Reject an unsupported compiler here rather than on a missing C++23 header deep
# in the build. Unparseable version strings (Apple clang) fall through to the
# header check in ToolchainChecks.cmake.
execute_process(
  COMMAND "${CORUNDUM_CLANGXX}" --version
  OUTPUT_VARIABLE _corundum_clang_version
  ERROR_QUIET
  OUTPUT_STRIP_TRAILING_WHITESPACE)
string(REGEX MATCH "[^\n]*" _corundum_clang_version_line "${_corundum_clang_version}")
if(_corundum_clang_version MATCHES "clang version ([0-9]+)")
  set(_corundum_clang_major "${CMAKE_MATCH_1}")
  if(_corundum_clang_major LESS 19)
    message(FATAL_ERROR
      "corundum: clang >= 19 required, found ${_corundum_clang_major} at ${CORUNDUM_CLANGXX}. "
      "Set -DLLVM_PREFIX=<prefix> to the pinned toolchain.")
  endif()
endif()
message(STATUS "corundum: ${_corundum_clang_version_line} at ${CORUNDUM_CLANGXX}")

# Preflight the forced standard library. project() runs the compiler and linker
# before ToolchainChecks.cmake can, so a missing libc++ would otherwise surface
# as a cryptic "cannot find -lc++" during compiler detection. Probe it here.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
  set(_corundum_probe_dir "${CMAKE_BINARY_DIR}/CMakeFiles")
  file(MAKE_DIRECTORY "${_corundum_probe_dir}")
  file(WRITE "${_corundum_probe_dir}/corundum_stdlib_probe.cpp"
    "#include <mdspan>\n#include <flat_map>\n#include <print>\n#include <expected>\nint main() { return 0; }\n")
  execute_process(
    COMMAND "${CORUNDUM_CLANGXX}" -std=c++23 -stdlib=libc++
            "${_corundum_probe_dir}/corundum_stdlib_probe.cpp"
            -o "${_corundum_probe_dir}/corundum_stdlib_probe${_corundum_exe_suffix}"
    RESULT_VARIABLE _corundum_stdlib_result
    ERROR_VARIABLE _corundum_stdlib_error
    OUTPUT_QUIET)
  if(_corundum_stdlib_result EQUAL 0)
    file(REMOVE "${_corundum_probe_dir}/corundum_stdlib_probe.cpp"
                "${_corundum_probe_dir}/corundum_stdlib_probe${_corundum_exe_suffix}")
  else()
    message(FATAL_ERROR
      "corundum: libc++ (with the C++23 headers) is required on this platform but is not\n"
      "usable with ${CORUNDUM_CLANGXX}. Install it and reconfigure (match your\n"
      "LLVM major version):\n"
      "  sudo apt install libc++-21-dev libc++abi-21-dev\n"
      "Compiler output:\n${_corundum_stdlib_error}")
  endif()
endif()

set(CMAKE_C_COMPILER      "${CORUNDUM_CLANG}"   CACHE FILEPATH "C compiler")
set(CMAKE_CXX_COMPILER    "${CORUNDUM_CLANGXX}" CACHE FILEPATH "C++ compiler")
set(CMAKE_OBJC_COMPILER   "${CORUNDUM_CLANG}"   CACHE FILEPATH "Objective-C compiler")
set(CMAKE_OBJCXX_COMPILER "${CORUNDUM_CLANGXX}" CACHE FILEPATH "Objective-C++ compiler")
if(CMAKE_HOST_WIN32)
  get_filename_component(_corundum_llvm_bindir "${CORUNDUM_CLANGXX}" DIRECTORY)
  set(CMAKE_RC_COMPILER "${_corundum_llvm_bindir}/llvm-rc${_corundum_exe_suffix}"
      CACHE FILEPATH "Resource compiler")
endif()

# Pin the linker so output doesn't depend on whichever default the host ships.
# Only on Linux (macOS behavior is unchanged) and only when lld is installed.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
  find_program(CORUNDUM_LLD
      NAMES ld.lld lld
      HINTS ${_corundum_llvm_hints}
      PATH_SUFFIXES bin
      ${_corundum_find_restrict})
  if(CORUNDUM_LLD)
    string(APPEND CMAKE_EXE_LINKER_FLAGS_INIT    " -fuse-ld=lld")
    string(APPEND CMAKE_SHARED_LINKER_FLAGS_INIT " -fuse-ld=lld")
  else()
    message(STATUS "corundum: lld not found; using the system default linker")
  endif()
endif()

# Force one standard library across the engine, the tools and every FetchContent
# dependency (it must be the same ABI). -stdlib=libc++ must reach the link line
# too, so set the linker flag initializers alongside the compile flag.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
  string(APPEND CMAKE_CXX_FLAGS_INIT           " -stdlib=libc++")
  string(APPEND CMAKE_EXE_LINKER_FLAGS_INIT    " -stdlib=libc++")
  string(APPEND CMAKE_SHARED_LINKER_FLAGS_INIT " -stdlib=libc++")
endif()
