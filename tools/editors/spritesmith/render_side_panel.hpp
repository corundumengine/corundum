#pragma once
#include "editor_state.hpp"
#include <corundum/tool_host/fonts.hpp>
#include <corundum/tool_host/ui_theme.hpp>

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
  void render_side_panel(EditorState &state, const corundum::tool_host::FontHandles &fonts,
                         const corundum::tool_host::ThemeColors &theme);

} // namespace tools::sprite
