#pragma once
#include "editor_state.hpp"
#include "tilemap_rendering.hpp"
#include "tileset_view.hpp"

namespace tools {
  struct ToolApp;
}

namespace tools::tilemap {

  /**
   * @brief Render the tileset tab bar and tile grid in the palette panel.
   *
   * Draws the tileset tab bar (ImGui) with add/remove controls, the tile
   * image grid (ImGui::Image), and a yellow selection highlight on the active
   * tile. Modifies @p texture_store and @p tileset_views when the user adds or
   * removes a sprite sheet.
   *
   * @note ImGui-only; issues no direct GLFW draw calls.
   * @warning Mutates @p state.
   *
   * @param app           ToolApp for texture operations.
   * @param state         Editor state (mutable).
   * @param texture_store Texture store, parallel to state.map.tilesets.
   * @param tileset_views Pre-resolved tileset views (must remain valid).
   */
  void render_tile_grid(ToolApp &app, EditorState &state, TilemapTextureStore &texture_store,
                        std::vector<TilesetView> &tileset_views);

} // namespace tools::tilemap
