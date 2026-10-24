# SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
# SPDX-License-Identifier: Apache-2.0
#
# Scratch-directory gate for the engine tests: a test that writes to disk does so under a
# corundum::test::TempDir (engine/tests/temp_dir.hpp), whose per-process token keeps concurrent
# runs (build/ and build-sanitized/) out of each other's directories. Run standalone with:
#
#   cmake -DROOT=<repo root> -P cmake/CheckTestTempDirs.cmake
#
# A file violates when it calls temp_directory_path() itself; temp_dir.hpp is the one file that
# may. Full-line comments are stripped first so prose doesn't trip the check.

if(NOT DEFINED ROOT)
  message(FATAL_ERROR "CheckTestTempDirs: pass -DROOT=<repo root>")
endif()

file(GLOB_RECURSE test_files "${ROOT}/engine/tests/*.cpp" "${ROOT}/engine/tests/*.hpp")
list(FILTER test_files EXCLUDE REGEX "/engine/tests/temp_dir\\.hpp$")

set(offenders)
foreach(file IN LISTS test_files)
  file(READ "${file}" content)

  # Strip full-line `//` and block-comment-continuation `*` lines before matching.
  string(REGEX REPLACE "[\r\n]+[ \t]*//[^\r\n]*" "\n" content "${content}")
  string(REGEX REPLACE "[\r\n]+[ \t]*\\*[^\r\n]*" "\n" content "${content}")

  if(content MATCHES "temp_directory_path")
    file(RELATIVE_PATH rel "${ROOT}" "${file}")
    list(APPEND offenders "${rel}")
  endif()
endforeach()

if(offenders)
  list(JOIN offenders "\n  " offenders_text)
  message(FATAL_ERROR "Test scratch-directory violation: use corundum::test::TempDir "
                      "(engine/tests/temp_dir.hpp) instead of temp_directory_path():\n  ${offenders_text}")
endif()

message(STATUS "Test scratch-directory check passed")
