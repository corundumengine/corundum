#pragma once
#include "editor_state.hpp"
#include <corundum/gameplay/world/tilemap/tilemap.hpp>

namespace tools::tilemap {

  /**
   * @brief True when @p layer_idx refers to the map's base/ground layer (z_index == 0).
   */
  [[nodiscard]] bool is_ground_layer(const corundum::gameplay::world::tilemap::Tilemap &map, int layer_idx) noexcept;

  /**
   * @brief Fill every empty (k_empty_tile) cell of the active layer with @p gid.
   *
   * No-op returning false if the active layer is not the ground layer
   * (is_ground_layer() is false). Already-painted cells are left untouched.
   * Snapshots the layer's tiles beforehand into state.fill_undo_tiles /
   * fill_undo_layer_idx so a single Ctrl+Z can revert the fill.
   *
   * @param state   Editor state to modify.
   * @param gid     Tile ID to stamp into empty cells.
   * @param flip    k_flip_h | k_flip_v bitmask applied to newly-filled cells.
   * @return true if the fill was applied, false if refused (active layer is
   *         not the ground layer).
   */
  [[nodiscard]] bool fill_ground_layer(EditorState &state, corundum::gameplay::world::tilemap::TileId gid,
                                       uint8_t flip);

} // namespace tools::tilemap
