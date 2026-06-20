#pragma once
#include "editor_state.hpp"

namespace tools::tilemap {

  /**
   * @brief Render the status bar at the bottom of the window.
   *
   * Shows the active layer name, selected tile GID, save hint, and exit hint.
   *
   * @param state    Current editor state (read-only).
   */
  void render_status_bar(const EditorState &state);

} // namespace tools::tilemap
