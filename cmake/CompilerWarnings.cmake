# Per-target compiler settings for first-party targets: language standard and
# warning policy.
#
# The warning level is applied per-target (never globally) via
# `corundum_enable_warnings(<target>)` so third-party dependencies and
# generated code are unaffected. Warnings-as-errors is deliberately NOT a
# compile option here: it is a developer/CI control exposed by CMake's
# CMAKE_COMPILE_WARNING_AS_ERROR, set in the shared `base` configure preset
# (and overridable with -DCMAKE_COMPILE_WARNING_AS_ERROR=OFF) so a new clang's
# warning doesn't permanently break a local build. Targets that cannot meet it
# (corundum_toolkit, whose vendored ImGui is not warning-clean) opt out with the
# COMPILE_WARNING_AS_ERROR target property.
#
# The C++ standard is set the same way: `corundum_use_cxx23(<target>)` marks
# the feature PUBLIC so it travels with the libraries, instead of relying on
# global CMAKE_CXX_STANDARD.

include_guard(GLOBAL)

function(corundum_enable_warnings target)
  if(NOT MSVC)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()

function(corundum_use_cxx23 target)
  target_compile_features(${target} PUBLIC cxx_std_23)
endfunction()

# Shipping code must never put an Engine, Scene or entities::World (each ~0.5 MB) on the stack:
# Windows' default main-thread stack is 1 MB. Warnings-as-errors turns an oversized frame into
# a build error. Not applied to test targets, which build Engines as locals and run on 8 MB stacks.
function(corundum_limit_stack_frames target)
  if(NOT MSVC)
    target_compile_options(${target} PRIVATE -Wframe-larger-than=131072)
  endif()
endfunction()
