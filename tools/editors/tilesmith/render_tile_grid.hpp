#pragma once
#include "editor_state.hpp"
#include "tilemap_rendering.hpp"
#include "tileset_view.hpp"

namespace corundum::tool_host {
  class ToolHost;
}

namespace tools::tilemap {

  /** @brief Render the tileset tab bar and tile grid in the palette panel. */
  void render_tile_grid(corundum::tool_host::ToolHost &host, EditorState &state, TilemapTextureStore &texture_store,
                        std::vector<TilesetView> &tileset_views);

} // namespace tools::tilemap
