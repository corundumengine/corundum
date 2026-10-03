# SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
# SPDX-License-Identifier: Apache-2.0
#
# Layering gate: the engine runtime (everything under engine/ except the gameplay/
# directories and engine/tests) must never depend on the gameplay framework. Run
# standalone with:
#
#   cmake -DROOT=<repo root> -P cmake/CheckLayering.cmake
#
# A file violates when it references gameplay by include (<corundum/gameplay/...>) or
# by name (a bare `gameplay::` token, `namespace gameplay::`, or `namespace
# corundum::gameplay`). Full-line comments are stripped first so prose doesn't churn
# the allowlist. ALLOWLIST is transitional debt: Session 2E empties it. The check
# also fails when an allowlisted file no longer violates, so the list can only shrink.

if(NOT DEFINED ROOT)
  message(FATAL_ERROR "CheckLayering: pass -DROOT=<repo root>")
endif()

# Transitional violations — engine files that still reach into gameplay. Empty in 2E.
set(ALLOWLIST
    engine/include/corundum/corundum.hpp
    engine/include/corundum/engine.hpp
    engine/include/corundum/world/scene.hpp
    engine/include/corundum/world/update.hpp
    engine/src/engine.cpp
    engine/src/world/update.cpp
)

file(GLOB_RECURSE engine_files
    "${ROOT}/engine/include/corundum/*.hpp"
    "${ROOT}/engine/src/*.hpp"
    "${ROOT}/engine/src/*.cpp"
)
list(FILTER engine_files EXCLUDE REGEX "/gameplay/")

set(violations)
foreach(file IN LISTS engine_files)
  file(READ "${file}" content)

  # Strip full-line `//` and block-comment-continuation `*` lines before matching.
  string(REGEX REPLACE "[\r\n]+[ \t]*//[^\r\n]*" "\n" content "${content}")
  string(REGEX REPLACE "[\r\n]+[ \t]*\\*[^\r\n]*" "\n" content "${content}")

  set(violates FALSE)
  if(content MATCHES "<corundum/gameplay/")
    set(violates TRUE)
  elseif(content MATCHES "(^|[^A-Za-z0-9_])gameplay::")
    set(violates TRUE)
  elseif(content MATCHES "namespace[ \t]+corundum::gameplay")
    set(violates TRUE)
  endif()

  if(violates)
    file(RELATIVE_PATH rel "${ROOT}" "${file}")
    list(APPEND violations "${rel}")
  endif()
endforeach()

set(offenders)
foreach(rel IN LISTS violations)
  if(NOT rel IN_LIST ALLOWLIST)
    list(APPEND offenders "${rel}")
  endif()
endforeach()

set(stale)
foreach(rel IN LISTS ALLOWLIST)
  if(NOT rel IN_LIST violations)
    list(APPEND stale "${rel}")
  endif()
endforeach()

if(offenders)
  list(JOIN offenders "\n  " offenders_text)
  message(FATAL_ERROR "Layering violation: engine runtime references corundum/gameplay:\n  ${offenders_text}")
endif()

list(LENGTH ALLOWLIST allowlist_count)

if(stale)
  list(JOIN stale "\n  " stale_text)
  message(FATAL_ERROR "Layering allowlist is stale — these files no longer violate; remove them:\n  ${stale_text}")
endif()

message(STATUS "Layering check passed (${allowlist_count} transitional violations allowed)")
