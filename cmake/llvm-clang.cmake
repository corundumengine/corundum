# ─────────────────────────────────────────────────────────────────────────────
# Pin the C/C++/Objective-C++ toolchain to Homebrew LLVM.
#
# Referenced from every CMakePresets.json entry via
#   "CMAKE_TOOLCHAIN_FILE": "${sourceDir}/cmake/llvm-clang.cmake"
#
# CMake loads this file before project() runs. We set CMAKE_*_COMPILER
# cache variables directly so compiler detection picks them up.
# ─────────────────────────────────────────────────────────────────────────────

if(DEFINED _LLVM_CLANG_CMAKE_LOADED)
  return()
endif()
set(_LLVM_CLANG_CMAKE_LOADED TRUE)

if(DEFINED LLVM_PREFIX AND NOT DEFINED ENV{LLVM_PREFIX})
  set(_llvm_bin "${LLVM_PREFIX}/bin")
elseif(DEFINED ENV{LLVM_PREFIX})
  set(_llvm_bin "$ENV{LLVM_PREFIX}/bin")
elseif(EXISTS "/opt/homebrew/opt/llvm/bin/clang")
  set(_llvm_bin "/opt/homebrew/opt/llvm/bin")
elseif(EXISTS "/usr/local/opt/llvm/bin/clang")
  set(_llvm_bin "/usr/local/opt/llvm/bin")
endif()

if(_llvm_bin)
  set(CMAKE_C_COMPILER      "${_llvm_bin}/clang"   CACHE FILEPATH "C compiler")
  set(CMAKE_CXX_COMPILER    "${_llvm_bin}/clang++" CACHE FILEPATH "C++ compiler")
  set(CMAKE_OBJC_COMPILER   "${_llvm_bin}/clang"   CACHE FILEPATH "Objective-C compiler")
  set(CMAKE_OBJCXX_COMPILER "${_llvm_bin}/clang++" CACHE FILEPATH "Objective-C++ compiler")
  message(STATUS "LLVM toolchain: ${_llvm_bin}")
endif()
