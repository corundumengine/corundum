#pragma once
#include "editor_state.hpp"
#include <algorithm>
#include <corundum/gameplay/world/tilemap/tilemap.hpp>

namespace tools::tilemap {

  /**
   * @brief Set a cell's elevation on a layer. No-op if out of bounds.
   *
   * Lazily grows layer.elevation from empty ("all flat") to width*height
   * (fill 0) on first non-flat write, preserving the field's documented
   * empty-means-flat invariant.
   *
   * @param state     Editor state to modify.
   * @param layer_idx Target layer index.
   * @param col       Tile column.
   * @param row       Tile row.
   * @param value     Elevation value to write, clamped to [0,100].
   */
  inline void set_elevation(EditorState &state, int layer_idx, int col, int row, uint8_t value) noexcept {
    if (layer_idx < 0 || layer_idx >= static_cast<int>(state.map.layers.size()))
      return;
    if (col < 0 || col >= state.map.width)
      return;
    if (row < 0 || row >= state.map.height)
      return;
    value = std::min<uint8_t>(value, 100);
    auto &layer = state.map.layers[static_cast<std::size_t>(layer_idx)];
    if (value == 0 && layer.elevation.empty())
      return; // already flat; avoid needless allocation
    if (layer.elevation.empty())
      layer.elevation.assign(static_cast<std::size_t>(state.map.width * state.map.height), 0);
    const std::size_t idx = static_cast<std::size_t>(row * state.map.width + col);
    layer.elevation[idx] = value;
  }

  /**
   * @brief Paint or erase elevation at the given window-space coordinates.
   *
   * No-op if the position is outside the canvas or map bounds.
   *
   * @param state  Editor state to modify.
   * @param win_x  Window-space X coordinate.
   * @param win_y  Window-space Y coordinate.
   * @param erase  True to reset elevation to 0; false to paint the selected value.
   */
  void paint_or_erase_elevation(EditorState &state, int win_x, int win_y, bool erase);

} // namespace tools::tilemap
