#pragma once
#include "editor_state.hpp"
#include <corundum/tool_host/ui_theme.hpp>

namespace tools::tilemap {

  /**
   * @brief Render the status bar at the bottom of the window.
   *
   * Shows the active layer name, selected tile, hover/mode info, dirty state, save hint, and exit
   * hint as separately colored segments (see corundum::tool_host::TextRole).
   *
   * @param state    Current editor state (read-only).
   * @param theme    Theme colors for per-segment text coloring.
   */
  void render_status_bar(const EditorState &state, const corundum::tool_host::ThemeColors &theme);

} // namespace tools::tilemap
