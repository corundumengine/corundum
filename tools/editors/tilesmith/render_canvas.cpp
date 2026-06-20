#include "render_canvas.hpp"
#include "render_collision.hpp"
#include "render_portal.hpp"

namespace tools::tilemap {

  namespace {

    void render_hover_tile(CanvasContext ctx, const EditorState &state) {
      if (state.hover_tile_col < 0 || state.hover_tile_row < 0)
        return;
      if (state.map.tilesets.empty())
        return;

      const float half_tw = static_cast<float>(state.map.diamond_w()) * state.tile_scale * 0.5f;
      const float half_th = static_cast<float>(state.map.diamond_h()) * state.tile_scale * 0.5f;
      const float ox = ctx.origin.x - state.camera.x;
      const float oy = ctx.origin.y - state.camera.y;
      const int col = state.hover_tile_col;
      const int row = state.hover_tile_row;

      // Bottom-center anchor: the southernmost vertex of the tile diamond in screen space.
      const float anchor_x = ox + static_cast<float>(col - row + state.map.height) * half_tw;
      const float anchor_y = oy + static_cast<float>(col + row + 2) * half_th;

      const auto fp = corundum::gameplay::world::tilemap::get_tile_footprint(state.map.tilesets, state.selected_gid);
      const float fw = static_cast<float>(fp.w);
      const float fh = static_cast<float>(fp.h);

      const ImVec2 top = {anchor_x - (fw - fh) * half_tw, anchor_y - (fw + fh) * half_th};
      const ImVec2 right = {anchor_x + fh * half_tw, anchor_y - fh * half_th};
      const ImVec2 bottom = {anchor_x, anchor_y};
      const ImVec2 left = {anchor_x - fw * half_tw, anchor_y - fw * half_th};

      constexpr ImU32 k_fill = IM_COL32(255, 255, 255, 28);
      constexpr ImU32 k_outline = IM_COL32(255, 220, 80, 210);
      ctx.dl->AddQuadFilled(top, right, bottom, left, k_fill);
      ctx.dl->AddQuad(top, right, bottom, left, k_outline, 1.5f);
    }

    void render_iso_grid(CanvasContext ctx, const EditorState &state) {
      if (state.map.tilesets.empty())
        return;
      const float half_tw = static_cast<float>(state.map.diamond_w()) * state.tile_scale * 0.5f;
      const float half_th = static_cast<float>(state.map.diamond_h()) * state.tile_scale * 0.5f;
      const float ox = ctx.origin.x - state.camera.x;
      const float oy = ctx.origin.y - state.camera.y;
      const int W = state.map.width;
      const int H = state.map.height;
      constexpr ImU32 k_color = IM_COL32(255, 255, 255, 40);

      // Column lines (k = 0..W): connect diamond top vertices along col k
      for (int k = 0; k <= W; ++k) {
        const ImVec2 p0{ox + static_cast<float>(k + H) * half_tw, oy + static_cast<float>(k) * half_th};
        const ImVec2 p1{ox + static_cast<float>(k) * half_tw, oy + static_cast<float>(k + H) * half_th};
        ctx.dl->AddLine(p0, p1, k_color, 1.f);
      }

      // Row lines (k = 0..H): connect diamond top vertices along row k
      for (int k = 0; k <= H; ++k) {
        const ImVec2 p0{ox + static_cast<float>(H - k) * half_tw, oy + static_cast<float>(k) * half_th};
        const ImVec2 p1{ox + static_cast<float>(W + H - k) * half_tw, oy + static_cast<float>(W + k) * half_th};
        ctx.dl->AddLine(p0, p1, k_color, 1.f);
      }
    }

  } // namespace

  void render_canvas(CanvasContext ctx, const EditorState &state, const MapRenderFn &render_map) {
    render_map(ctx);

    if (state.show_grid)
      render_iso_grid(ctx, state);

    if (state.show_collisions) {
      render_collisions(ctx, state);
      render_collision_preview(ctx, state);
    } else {
      render_hover_tile(ctx, state);
      render_erase_preview(ctx, state);
    }

    if (state.show_portals) {
      render_portals(ctx, state);
      render_portal_preview(ctx, state);
    }
  }

} // namespace tools::tilemap
