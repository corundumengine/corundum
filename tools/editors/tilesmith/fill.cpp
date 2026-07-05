#include "fill.hpp"
#include "paint.hpp"

namespace tools::tilemap {

  bool is_ground_layer(const corundum::gameplay::world::tilemap::Tilemap &map, int layer_idx) noexcept {
    if (layer_idx < 0 || layer_idx >= static_cast<int>(map.layers.size()))
      return false;
    return map.layers[static_cast<std::size_t>(layer_idx)].z_index == 0;
  }

  bool fill_ground_layer(EditorState &state, corundum::gameplay::world::tilemap::TileId gid, uint8_t flip) {
    if (!is_ground_layer(state.map, state.active_layer))
      return false;

    const int layer_idx = state.active_layer;
    auto &layer = state.map.layers[static_cast<std::size_t>(layer_idx)];
    state.fill_undo_tiles = layer.tiles;
    state.fill_undo_layer_idx = layer_idx;

    for (int row = 0; row < state.map.height; ++row) {
      for (int col = 0; col < state.map.width; ++col) {
        const int idx = row * state.map.width + col;
        const std::size_t uidx = static_cast<std::size_t>(idx);
        if (uidx < layer.tiles.size() && layer.tiles[uidx] == corundum::gameplay::world::tilemap::k_empty_tile)
          set_tile(state, layer_idx, col, row, gid, flip);
      }
    }
    state.dirty = true;
    return true;
  }

} // namespace tools::tilemap
