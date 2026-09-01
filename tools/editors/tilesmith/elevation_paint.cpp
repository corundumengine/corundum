#include "elevation_paint.hpp"
#include "coords.hpp"
#include "layout.hpp"

namespace tools::tilesmith {

  void paint_or_erase_elevation(EditorState &state, int win_x, int win_y, bool erase) {
    if (state.map.tilesets.empty())
      return;
    const auto tc =
        screen_to_tile(win_x, win_y, 0, k_menu_h, CANVAS_W, CANVAS_H, state.canvas.offset_x, state.canvas.offset_y,
                       state.canvas.scale, state.elev_step_px, state.map.width, state.map.height,
                       effective_diamond_w(state.map), effective_diamond_h(state.map), state.map);
    if (!tc)
      return;
    set_elevation(state, state.active_layer, tc->col, tc->row, erase ? 0 : state.selected_elevation);
    state.dirty = true;
  }

} // namespace tools::tilesmith
