// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "editor_state.hpp"
#include <corundum/toolkit/widgets/ui_theme.hpp>

namespace tools::tilesmith {

  /**
   * @brief Render the status bar at the bottom of the window.
   *
   * Shows the active layer name, selected tile, hover/mode info, dirty state, save hint, and exit
   * hint as separately colored segments (see corundum::toolkit::TextRole).
   *
   * @param state    Current editor state (read-only).
   * @param theme    Theme colors for per-segment text coloring.
   */
  void render_status_bar(const EditorState &state, const corundum::toolkit::ThemeColors &theme);

} // namespace tools::tilesmith
