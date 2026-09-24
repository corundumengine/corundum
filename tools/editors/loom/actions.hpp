// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "editor_state.hpp"
#include <corundum/toolkit/host/tool_host.hpp>

namespace tools::loom {

  void action_open(EditorState &state);
  void action_save_as(EditorState &state);
  void action_save(EditorState &state, corundum::toolkit::host::ToolHost &host);

} // namespace tools::loom
