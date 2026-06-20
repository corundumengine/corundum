#pragma once
#include "common/ui_theme.hpp"
#include "editor_state.hpp"
#include "imgui_fonts.hpp"

namespace tools::sprite {

  /**
   * @brief Render the right-side ImGui property panel.
   *
   * Displays sheet metadata, grid configuration, and mode-specific sprite or
   * clip editing controls. Mutates @p state in response to user interaction.
   *
   * @param state  Editor state to read and modify.
   * @param fonts  Loaded ImGui font handles.
   * @param theme  Semantic color theme.
   */
  void render_side_panel(EditorState &state, const FontHandles &fonts, const tools::theme::ThemeColors &theme);

} // namespace tools::sprite
