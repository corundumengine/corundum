#pragma once
#include "common/tool_texture.hpp"
#include "editor_state.hpp"

namespace tools {
  struct ToolApp;
}

namespace tools::sprite {

  using ToolTexture = tools::common::ToolTexture;

  /// Render the permanent "Preview" section in the side panel.
  /// Always visible; shows a placeholder until a sprite and animation are selected.
  void render_preview_panel(ToolApp &app, const EditorState &state, const ToolTexture &tex, float dt_seconds);

} // namespace tools::sprite
