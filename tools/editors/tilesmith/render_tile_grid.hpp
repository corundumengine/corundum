// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "editor_state.hpp"
#include "tilemap_rendering.hpp"
#include "tileset_view.hpp"

namespace corundum::toolkit::host {
  class ToolHost;
}

namespace tools::tilesmith {

  /** @brief Render the tileset tab bar and tile grid in the palette panel. */
  void render_tile_grid(corundum::toolkit::host::ToolHost &host, EditorState &state, TilemapTextureStore &texture_store,
                        std::vector<TilesetView> &tileset_views);

} // namespace tools::tilesmith
