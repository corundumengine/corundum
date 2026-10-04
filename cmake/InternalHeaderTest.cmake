include_guard(GLOBAL)

# Add a test that compiles an internal engine header directly. When the header
# isn't reachable (vendored/consumer checkout), the test is silently skipped.
#
# The include directory is derived from the header's parent directory. This
# supports one include directory and one source file; tests that need more
# (e.g. FontAtlas, which also links freetype and adds two include dirs) stay
# as hand-rolled blocks.
#
# corundum_add_internal_header_test(<target> <header> <source> <skip_message>)
function(corundum_add_internal_header_test target header source skip_message)
  if(EXISTS "${header}")
    get_filename_component(_dir "${header}" DIRECTORY)
    target_sources(${target} PRIVATE ${source})
    target_include_directories(${target} PRIVATE ${_dir})
  else()
    message(STATUS "${skip_message}")
  endif()
endfunction()
