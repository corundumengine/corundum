# Single source of truth for locating the pinned LLVM install. The build
# toolchain (llvm-clang.cmake), the clang-format target (FormatAndDocs.cmake)
# and scripts/run_tidy.sh must all agree on one prefix; deriving it separately
# in each consumer let them drift.
#
# Resolution order: explicit `-DLLVM_PREFIX` (cache) > `$LLVM_PREFIX` (env) >
# common macOS LLVM prefixes > LLVM's default install on Windows > the
# highest-numbered distro LLVM under /usr/lib/llvm-*.
#
# corundum_resolve_llvm_prefix(<out-var>) returns the prefix, or an empty string
# when none is known — callers then fall back to PATH and report what they use.
include_guard(GLOBAL)

function(corundum_resolve_llvm_prefix out_var)
  # A cache/env prefix is authoritative even if the path is wrong; the caller
  # fails loudly rather than silently using a different compiler.
  if(LLVM_PREFIX)
    set(${out_var} "${LLVM_PREFIX}" PARENT_SCOPE)
    return()
  endif()

  if(DEFINED ENV{LLVM_PREFIX} AND NOT "$ENV{LLVM_PREFIX}" STREQUAL "")
    set(${out_var} "$ENV{LLVM_PREFIX}" PARENT_SCOPE)
    return()
  endif()

  # Toolchain files run before project(), so CMAKE_EXECUTABLE_SUFFIX is not set.
  set(_corundum_exe_suffix "")
  if(CMAKE_HOST_WIN32)
    set(_corundum_exe_suffix ".exe")
  endif()

  # Common macOS LLVM prefixes — other macOS installs resolve via LLVM_PREFIX or
  # PATH — plus LLVM's official installer default on Windows.
  foreach(candidate /opt/homebrew/opt/llvm /usr/local/opt/llvm "C:/Program Files/LLVM")
    if(EXISTS "${candidate}/bin/clang++${_corundum_exe_suffix}")
      set(${out_var} "${candidate}" PARENT_SCOPE)
      return()
    endif()
  endforeach()

  # Distro LLVM on Linux: /usr/lib/llvm-<major>. Prefer the newest.
  file(GLOB distro_candidates "/usr/lib/llvm-*")
  list(SORT distro_candidates COMPARE NATURAL ORDER DESCENDING)
  foreach(candidate IN LISTS distro_candidates)
    if(EXISTS "${candidate}/bin/clang++")
      set(${out_var} "${candidate}" PARENT_SCOPE)
      return()
    endif()
  endforeach()

  set(${out_var} "" PARENT_SCOPE)
endfunction()
