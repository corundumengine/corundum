#include "paint.hpp"
#include "coords.hpp"
#include "layout.hpp"

namespace tools::tilemap {

  void paint_or_erase(EditorState &state, int win_x, int win_y, bool erase) {
    if (state.map.tilesets.empty())
      return;
    const auto tc = screen_to_tile(win_x, win_y, 0, 0, CANVAS_W, CANVAS_H, state.camera.x, state.camera.y,
                                   state.tile_scale, state.map.width, state.map.height, effective_diamond_w(state.map),
                                   effective_diamond_h(state.map));
    if (!tc)
      return;
    set_tile(state, state.active_layer, tc->col, tc->row,
             erase ? corundum::gameplay::world::tilemap::k_empty_tile : state.selected_gid,
             erase ? 0 : state.selected_flip);
    state.dirty = true;
  }

  void erase_rect(EditorState &state, int col_min, int row_min, int col_max, int row_max) {
    for (int row = row_min; row <= row_max; ++row)
      for (int col = col_min; col <= col_max; ++col)
        set_tile(state, state.active_layer, col, row, corundum::gameplay::world::tilemap::k_empty_tile);
  }

} // namespace tools::tilemap
