#pragma once
#include "editor_state.hpp"
#include <corundum/world/tilemap/tilemap.hpp>

namespace tools::tilemap {

  /**
   * @brief True when @p layer_idx refers to the map's base/ground layer (z_index == 0).
   */
  [[nodiscard]] bool is_ground_layer(const corundum::world::tilemap::Tilemap &map, int layer_idx) noexcept;

  /**
   * @brief Fill every empty (k_empty_tile) cell of the active layer with @p gid.
   *
   * No-op returning false if the active layer is not the ground layer
   * (is_ground_layer() is false). Already-painted cells are left untouched.
   * Pushes one undo checkpoint at the end so Ctrl+Z reverts the whole fill in
   * a single step.
   *
   * @param state   Editor state to modify.
   * @param gid     Tile ID to stamp into empty cells.
   * @param flip    k_flip_h | k_flip_v bitmask applied to newly-filled cells.
   * @return true if the fill was applied, false if refused (active layer is
   *         not the ground layer).
   */
  [[nodiscard]] bool fill_ground_layer(EditorState &state, corundum::world::tilemap::TileId gid, uint8_t flip);

} // namespace tools::tilemap
