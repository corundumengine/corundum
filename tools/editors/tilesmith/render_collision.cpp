#include "render_collision.hpp"
#include "coords.hpp"

namespace tools::tilemap {
  using namespace tools::common;

  namespace {

    // Convert a Tiled-pixel position (x_px, y_px) to isometric screen space.
    // x_px = col * tile_w, y_px = row * tile_h (sub-tile fractions allowed).
    inline ImVec2 tiled_to_iso(const CanvasContext &ctx, float x_px, float y_px, float inv_tw, float inv_th,
                               float half_tw, float half_th, float x_shift, float cam_x, float cam_y) noexcept {
      const float col_f = x_px * inv_tw;
      const float row_f = y_px * inv_th;
      return {ctx.origin.x + (col_f - row_f) * half_tw + x_shift - cam_x,
              ctx.origin.y + (col_f + row_f) * half_th - cam_y};
    }

    // Draw a Tiled-pixel rect as an isometric rhombus (4-vertex quad).
    void draw_iso_rect(const CanvasContext &ctx, float x, float y, float w, float h, float inv_tw, float inv_th,
                       float half_tw, float half_th, float x_shift, float cam_x, float cam_y, ImU32 fill,
                       ImU32 outline) noexcept {
      const ImVec2 top = tiled_to_iso(ctx, x, y, inv_tw, inv_th, half_tw, half_th, x_shift, cam_x, cam_y);
      const ImVec2 right = tiled_to_iso(ctx, x + w, y, inv_tw, inv_th, half_tw, half_th, x_shift, cam_x, cam_y);
      const ImVec2 bottom = tiled_to_iso(ctx, x + w, y + h, inv_tw, inv_th, half_tw, half_th, x_shift, cam_x, cam_y);
      const ImVec2 left = tiled_to_iso(ctx, x, y + h, inv_tw, inv_th, half_tw, half_th, x_shift, cam_x, cam_y);
      ctx.dl->AddQuadFilled(top, right, bottom, left, fill);
      ctx.dl->AddQuad(top, right, bottom, left, outline, 1.f);
    }

  } // namespace

  void render_collisions(CanvasContext ctx, const EditorState &state) {
    if (state.map.tilesets.empty())
      return;
    const int dw = effective_diamond_w(state.map);
    const int dh = effective_diamond_h(state.map);
    const float half_tw = static_cast<float>(dw) * state.tile_scale * 0.5f;
    const float half_th = static_cast<float>(dh) * state.tile_scale * 0.5f;
    const float x_shift = static_cast<float>(state.map.height) * half_tw;
    const int tw = state.map.tilesets[0].info.tile_width;
    const int th = state.map.tilesets[0].info.tile_height;
    const float inv_tw = 1.f / static_cast<float>(tw);
    const float inv_th = 1.f / static_cast<float>(th);

    const auto &cols = state.map.collisions;
    for (std::size_t i = 0; i < cols.size(); ++i) {
      draw_iso_rect(ctx, cols.cols[i] * tw, cols.rows[i] * th, cols.col_spans[i] * tw, cols.row_spans[i] * th, inv_tw,
                    inv_th, half_tw, half_th, x_shift, state.camera.x, state.camera.y, IM_COL32(255, 80, 80, 60),
                    IM_COL32(255, 80, 80, 200));
    }

    const auto &tris = state.map.collision_triangles;
    for (std::size_t i = 0; i < tris.size(); ++i) {
      draw_iso_rect(ctx, tris.cols[i] * tw, tris.rows[i] * th, tris.col_spans[i] * tw, tris.row_spans[i] * th, inv_tw,
                    inv_th, half_tw, half_th, x_shift, state.camera.x, state.camera.y, IM_COL32(255, 140, 60, 70),
                    IM_COL32(255, 140, 60, 210));
    }

    if (state.show_collisions && state.triangle_collision_mode && state.hover_tile_col >= 0 &&
        state.hover_tile_row >= 0) {
      const float col = static_cast<float>(state.hover_tile_col);
      const float row = static_cast<float>(state.hover_tile_row);
      draw_iso_rect(ctx, col * tw, row * th, static_cast<float>(tw), static_cast<float>(th), inv_tw, inv_th, half_tw,
                    half_th, x_shift, state.camera.x, state.camera.y, IM_COL32(100, 255, 100, 50),
                    IM_COL32(100, 255, 100, 220));
    }
  }

  void render_collision_preview(CanvasContext ctx, const EditorState &state) {
    if (!state.collision_dragging || state.map.tilesets.empty())
      return;
    const int dw = effective_diamond_w(state.map);
    const int dh = effective_diamond_h(state.map);
    const float half_tw = static_cast<float>(dw) * state.tile_scale * 0.5f;
    const float half_th = static_cast<float>(dh) * state.tile_scale * 0.5f;
    const float x_shift = static_cast<float>(state.map.height) * half_tw;
    const int tw = state.map.tilesets[0].info.tile_width;
    const int th = state.map.tilesets[0].info.tile_height;
    const float inv_tw = 1.f / static_cast<float>(tw);
    const float inv_th = 1.f / static_cast<float>(th);

    corundum::gameplay::world::tilemap::CollisionRect cr;
    if (state.col_drag_sub_tile) {
      cr = pixel_to_tiled_rect(state.col_drag_anchor_win_x, state.col_drag_anchor_win_y, state.col_drag_cur_win_x,
                               state.col_drag_cur_win_y, state.camera.x, state.camera.y, state.tile_scale,
                               static_cast<float>(tw), static_cast<float>(th));
    } else {
      cr = snap_to_tile_rect(state.col_drag_anchor_col, state.col_drag_anchor_row, state.col_drag_cur_col,
                             state.col_drag_cur_row);
    }
    draw_iso_rect(ctx, cr.col * tw, cr.row * th, cr.col_span * tw, cr.row_span * th, inv_tw, inv_th, half_tw, half_th,
                  x_shift, state.camera.x, state.camera.y, IM_COL32(100, 255, 100, 40), IM_COL32(100, 255, 100, 230));
  }

  void render_erase_preview(CanvasContext ctx, const EditorState &state) {
    if (!state.erase_dragging || state.map.tilesets.empty())
      return;
    const int dw = effective_diamond_w(state.map);
    const int dh = effective_diamond_h(state.map);
    const float half_tw = static_cast<float>(dw) * state.tile_scale * 0.5f;
    const float half_th = static_cast<float>(dh) * state.tile_scale * 0.5f;
    const float x_shift = static_cast<float>(state.map.height) * half_tw;
    const int tw = state.map.tilesets[0].info.tile_width;
    const int th = state.map.tilesets[0].info.tile_height;
    const float inv_tw = 1.f / static_cast<float>(tw);
    const float inv_th = 1.f / static_cast<float>(th);

    const auto cr = snap_to_tile_rect(state.erase_drag_anchor_col, state.erase_drag_anchor_row,
                                      state.erase_drag_cur_col, state.erase_drag_cur_row);
    draw_iso_rect(ctx, cr.col * tw, cr.row * th, cr.col_span * tw, cr.row_span * th, inv_tw, inv_th, half_tw, half_th,
                  x_shift, state.camera.x, state.camera.y, IM_COL32(255, 100, 60, 50), IM_COL32(255, 100, 60, 220));
  }

} // namespace tools::tilemap
