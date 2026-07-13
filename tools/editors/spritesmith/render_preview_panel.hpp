#pragma once
#include "editor_state.hpp"
#include <corundum/platform/texture_cache.hpp>

namespace corundum::tool_host {
  class ToolHost;
}

namespace tools::sprite {

  /// Render the permanent "Preview" section in the side panel.
  void render_preview_panel(corundum::tool_host::ToolHost &host, const EditorState &state,
                            const corundum::platform::TextureInfo &tex, float dt_seconds);

} // namespace tools::sprite
