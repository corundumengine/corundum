#pragma once
#include "editor_state.hpp"
#include <corundum/gameplay/world/tilemap/tilemap.hpp>

namespace tools::tilemap {

  /**
   * @brief Set a tile in a layer at (col, row). No-op if out of bounds.
   *
   * When @p gid is k_empty_tile or @p flip is 0 the flip entry for the cell is removed.
   * When @p flip is non-zero it is stored in the layer's flip_flags map.
   *
   * @param state     Editor state to modify.
   * @param layer_idx Target layer index.
   * @param col       Tile column.
   * @param row       Tile row.
   * @param gid       Tile ID to write.
   * @param flip      k_flip_h | FLIP_V bitmask (0 = no flip).
   */
  inline void set_tile(EditorState &state, int layer_idx, int col, int row,
                       corundum::gameplay::world::tilemap::TileId gid, uint8_t flip = 0) noexcept {
    if (layer_idx < 0 || layer_idx >= static_cast<int>(state.map.layers.size()))
      return;
    if (col < 0 || col >= state.map.width)
      return;
    if (row < 0 || row >= state.map.height)
      return;
    auto &layer = state.map.layers[static_cast<std::size_t>(layer_idx)];
    const int idx = row * state.map.width + col;
    layer.tiles[static_cast<std::size_t>(idx)] = gid;
    if (gid == corundum::gameplay::world::tilemap::k_empty_tile || flip == 0)
      layer.flip_flags.erase(idx);
    else
      layer.flip_flags[idx] = flip;
  }

  /**
   * @brief Paint or erase a tile at the given window-space coordinates.
   *
   * No-op if the position is outside the canvas or map bounds.
   *
   * @param state  Editor state to modify.
   * @param win_x  Window-space X coordinate.
   * @param win_y  Window-space Y coordinate.
   * @param erase  True to clear the tile; false to paint the selected GID.
   */
  void paint_or_erase(EditorState &state, int win_x, int win_y, bool erase);

  /**
   * @brief Erase all tiles on the active layer within a tile-coordinate bounding box.
   *
   * @param state    Editor state to modify.
   * @param col_min  Left tile column (inclusive).
   * @param row_min  Top tile row (inclusive).
   * @param col_max  Right tile column (inclusive).
   * @param row_max  Bottom tile row (inclusive).
   */
  void erase_rect(EditorState &state, int col_min, int row_min, int col_max, int row_max);

} // namespace tools::tilemap
