#pragma once
#include "editor_state.hpp"

namespace tools::sprite {

  /**
   * @brief Render the bottom status bar ImGui window.
   *
   * Shows the active sheet mode, file path, hover frame coordinate, recording
   * state, and unsaved-changes indicator.
   *
   * @param state Current editor state (read-only).
   */
  void render_status_bar(const EditorState &state);

} // namespace tools::sprite
