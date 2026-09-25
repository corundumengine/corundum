# Per-target compiler settings for first-party targets: language standard and
# warning policy.
#
# -Werror is mandatory for first-party C++: the compiler is pinned to Homebrew
# LLVM clang, and warnings are treated as errors in CI and local builds alike.
# These flags are applied per-target (never globally) via
# `corundum_enable_warnings(<target>)` so third-party dependencies and
# generated code are unaffected.
#
# The C++ standard is set the same way: `corundum_use_cxx23(<target>)` marks
# the feature PUBLIC so it travels with the libraries, instead of relying on
# global CMAKE_CXX_STANDARD.

include_guard(GLOBAL)

function(corundum_enable_warnings target)
  if(NOT MSVC)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
endfunction()

function(corundum_use_cxx23 target)
  target_compile_features(${target} PUBLIC cxx_std_23)
endfunction()

# Shipping code must never put an Engine, Scene or entities::World (each ~0.5 MB) on the stack:
# Windows' default main-thread stack is 1 MB. -Werror turns an oversized frame into a build
# error. Not applied to test targets, which build Engines as locals and run on 8 MB stacks.
function(corundum_limit_stack_frames target)
  if(NOT MSVC)
    target_compile_options(${target} PRIVATE -Wframe-larger-than=131072)
  endif()
endfunction()
