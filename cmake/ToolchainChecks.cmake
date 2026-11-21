# Fail configure with a readable message when the selected toolchain cannot
# build the engine's C++23 library surface. Without this, a libstdc++ that
# predates GCC 15 fails with a cascade of "file not found" errors hundreds of
# compile units into the build — the failure that motivated this module.
#
# A failing probe prints the compiler's own output, so the underlying error
# (a missing header, or an STL that rejects this clang) is visible directly.
#
# Must run after project() (the probe needs an enabled C++ compiler) and before
# dependencies are fetched, so it fails fast.
include_guard(GLOBAL)

# Each platform's likely cause, printed above the compiler output.
if(WIN32)
  string(CONCAT _corundum_stdlib_hint
    "clang uses the MSVC STL of the active Visual Studio. An STL1000 error below "
    "means that STL needs a newer clang than this one: upgrade LLVM or point "
    "-DLLVM_PREFIX at a newer install.")
elseif(APPLE)
  string(CONCAT _corundum_stdlib_hint
    "This clang's libc++ is too old: upgrade LLVM (brew upgrade llvm) or point "
    "-DLLVM_PREFIX at a newer install.")
else()
  string(CONCAT _corundum_stdlib_hint
    "The C++ standard library predates GCC 15. Build against libc++ (install "
    "libc++-21-dev libc++abi-21-dev for your LLVM major and use corundum's "
    "cmake/llvm-clang.cmake, or pass -stdlib=libc++) and reconfigure.")
endif()

# Headers whose absence means the standard library is too old, not the code.
set(_corundum_required_headers mdspan flat_map print expected)
foreach(header IN LISTS _corundum_required_headers)
  # NO_CACHE re-probes on every configure, so installing libc++ and re-running
  # CMake succeeds without a manual cache purge.
  try_compile(_corundum_have_header
    SOURCE_FROM_CONTENT "probe_${header}.cpp" "#include <${header}>\nint main() { return 0; }\n"
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    NO_CACHE
    OUTPUT_VARIABLE _corundum_probe_output)
  if(NOT _corundum_have_header)
    message(FATAL_ERROR
      "corundum: <${header}> is unavailable with ${CMAKE_CXX_COMPILER} (clang ${CMAKE_CXX_COMPILER_VERSION}).\n"
      "${_corundum_stdlib_hint}\n"
      "Compiler output:\n${_corundum_probe_output}")
  endif()
endforeach()
