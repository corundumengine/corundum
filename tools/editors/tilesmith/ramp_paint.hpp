#pragma once
#include "editor_state.hpp"
#include <corundum/gameplay/world/tilemap/tilemap.hpp>
#include <optional>

namespace tools::tilemap {

  /**
   * @brief Set or erase a cell's ramp axis on a layer. No-op if out of bounds.
   *
   * @param state     Editor state to modify.
   * @param layer_idx Target layer index.
   * @param col       Tile column.
   * @param row       Tile row.
   * @param axis      Axis to write, or std::nullopt to erase (cell is no longer a ramp).
   */
  inline void set_ramp(EditorState &state, int layer_idx, int col, int row,
                       std::optional<corundum::gameplay::world::tilemap::RampAxis> axis) noexcept {
    if (layer_idx < 0 || layer_idx >= static_cast<int>(state.map.layers.size()))
      return;
    if (col < 0 || col >= state.map.width)
      return;
    if (row < 0 || row >= state.map.height)
      return;
    auto &layer = state.map.layers[static_cast<std::size_t>(layer_idx)];
    const int idx = row * state.map.width + col;
    if (axis)
      layer.ramps[idx] = *axis;
    else
      layer.ramps.erase(idx);
  }

  /**
   * @brief Paint or erase a ramp at the given window-space coordinates.
   *
   * No-op if the position is outside the canvas or map bounds.
   *
   * @param state  Editor state to modify.
   * @param win_x  Window-space X coordinate.
   * @param win_y  Window-space Y coordinate.
   * @param erase  True to clear the ramp; false to paint the selected axis.
   */
  void paint_or_erase_ramp(EditorState &state, int win_x, int win_y, bool erase);

} // namespace tools::tilemap
