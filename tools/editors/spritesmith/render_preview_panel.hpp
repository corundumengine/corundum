// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "editor_state.hpp"
#include <corundum/platform/texture_cache.hpp>

namespace corundum::toolkit {
  class ToolHost;
}

namespace tools::spritesmith {

  /// Render the permanent "Preview" section in the side panel.
  void render_preview_panel(corundum::toolkit::ToolHost &host, const EditorState &state,
                            const corundum::platform::TextureInfo &tex, float dt_seconds);

} // namespace tools::spritesmith
