#pragma once
#include "common/imgui_fonts.hpp"
#include "common/ui_theme.hpp"
#include "editor_state.hpp"

namespace tools::tilemap {

  using FontHandles = tools::common::FontHandles;

  /**
   * @brief Render the layer strip inside the current ImGui window.
   *
   * Draws a title row and one row per layer using the window draw list for
   * background fills and ImGui text for layer names and visibility icons.
   * Call from within the panel child window before other panel content.
   *
   * @param state  Current editor state.
   * @param fonts  ImGui font handles (icons font used for visibility toggle).
   * @param theme  Theme colors for semantic icon coloring.
   */
  void render_layer_strip(const EditorState &state, const FontHandles &fonts, const tools::theme::ThemeColors &theme);

} // namespace tools::tilemap
