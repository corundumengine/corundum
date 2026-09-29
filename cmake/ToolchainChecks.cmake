# Fail configure with a readable message when the selected toolchain cannot
# build the engine's C++23 library surface. Without this, a libstdc++ that
# predates GCC 15 fails with a cascade of "file not found" errors hundreds of
# compile units into the build — the failure that motivated this module.
#
# Must run after project() (check_cxx_source_compiles needs an enabled C++
# compiler) and before dependencies are fetched, so it fails fast.
include_guard(GLOBAL)

include(CheckCXXSourceCompiles)

# corundum_use_cxx23() sets the standard per target; mirror it for the probe.
set(_corundum_saved_required_flags "${CMAKE_REQUIRED_FLAGS}")
string(APPEND CMAKE_REQUIRED_FLAGS " -std=c++23")

# Headers whose absence means the standard library is too old, not the code.
set(_corundum_required_headers mdspan flat_map print expected)
foreach(header IN LISTS _corundum_required_headers)
  # Re-probe every configure so that installing libc++ and re-running CMake
  # succeeds without a manual cache purge (a cached failure would otherwise
  # keep the check from running again).
  unset(CORUNDUM_HAVE_${header} CACHE)
  check_cxx_source_compiles(
    "#include <${header}>\nint main() { return 0; }"
    CORUNDUM_HAVE_${header})
  if(NOT CORUNDUM_HAVE_${header})
    message(FATAL_ERROR
      "corundum: <${header}> is unavailable with this toolchain.\n"
      "On Linux this means the C++ standard library predates GCC 15. Build against "
      "libc++ (install libc++-21-dev libc++abi-21-dev for your LLVM major and use corundum's "
      "cmake/llvm-clang.cmake, or pass -stdlib=libc++) and reconfigure.")
  endif()
endforeach()

set(CMAKE_REQUIRED_FLAGS "${_corundum_saved_required_flags}")
