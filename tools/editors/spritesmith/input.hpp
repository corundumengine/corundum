#pragma once
#include "editor_state.hpp"

namespace tools::spritesmith {

  /**
   * @brief Process all editor input for one frame using ImGui IO.
   *
   * Reads key and mouse state from ImGui::GetIO() and mutates @p state
   * accordingly. Sets @p running to false when the user requests quit.
   *
   * @pre Must be called inside an active ImGui frame (after simgui_new_frame).
   *
   * @param state    Editor state to modify.
   * @param running  Set to false to exit the application.
   */
  void handle_input(EditorState &state, bool &running);

} // namespace tools::spritesmith
