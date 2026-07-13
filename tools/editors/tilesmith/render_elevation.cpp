#include "render_elevation.hpp"
#include "coords.hpp"
#include <algorithm>

namespace tools::tilemap {

  namespace {

    // Convert tile-grid coords (col, row) to isometric screen position, lifted by
    // elevation so the tint sits on the tile's actual (raised) rendered position.
    inline ImVec2 elev_tile_to_iso(const CanvasContext &ctx, float col_f, float row_f, float elev, float half_tw,
                                   float half_th, float elev_step, float x_shift, float cam_x, float cam_y) noexcept {
      return {ctx.origin.x + (col_f - row_f) * half_tw + x_shift - cam_x,
              ctx.origin.y + (col_f + row_f) * half_th - elev * elev_step - cam_y};
    }

    void draw_iso_cell(const CanvasContext &ctx, float col, float row, float elev, float half_tw, float half_th,
                       float elev_step, float x_shift, float cam_x, float cam_y, ImU32 fill, ImU32 outline) noexcept {
      const ImVec2 top = elev_tile_to_iso(ctx, col, row, elev, half_tw, half_th, elev_step, x_shift, cam_x, cam_y);
      const ImVec2 right =
          elev_tile_to_iso(ctx, col + 1.f, row, elev, half_tw, half_th, elev_step, x_shift, cam_x, cam_y);
      const ImVec2 bottom =
          elev_tile_to_iso(ctx, col + 1.f, row + 1.f, elev, half_tw, half_th, elev_step, x_shift, cam_x, cam_y);
      const ImVec2 left =
          elev_tile_to_iso(ctx, col, row + 1.f, elev, half_tw, half_th, elev_step, x_shift, cam_x, cam_y);
      ctx.dl->AddQuadFilled(top, right, bottom, left, fill);
      if (outline)
        ctx.dl->AddQuad(top, right, bottom, left, outline, 1.f);
    }

    // Cool blue (low) -> warm red (high) lerp for elevation tint.
    ImU32 elevation_fill_color(uint8_t v) {
      const float t = std::clamp(static_cast<float>(v) / 100.f, 0.f, 1.f);
      const int r = static_cast<int>(40.f + t * (230.f - 40.f));
      const int g = static_cast<int>(90.f + t * (60.f - 90.f));
      const int b = static_cast<int>(220.f - t * (220.f - 40.f));
      return IM_COL32(r, g, b, 70);
    }

  } // namespace

  void render_elevation_overlay(CanvasContext ctx, const EditorState &state) {
    if (state.map.tilesets.empty() || state.active_layer >= static_cast<int>(state.map.layers.size()))
      return;
    const auto &layer = state.map.layers[static_cast<std::size_t>(state.active_layer)];
    if (layer.elevation.empty())
      return; // all flat: nothing to tint

    const int dw = effective_diamond_w(state.map);
    const int dh = effective_diamond_h(state.map);
    const auto iso = corundum::core::math::compute_iso_params(dw, dh, state.map.height, state.canvas.scale);
    const auto tiles = state.map.layer_view(layer);

    for (int row = 0; row < state.map.height; ++row) {
      for (int col = 0; col < state.map.width; ++col) {
        const int cell_idx = row * state.map.width + col;
        const uint8_t v = layer.elevation[static_cast<std::size_t>(cell_idx)];
        if (v == 0)
          continue; // skip flat cells to reduce clutter on mostly-flat maps
        // Skip cells with no tile on this layer — otherwise the tint looks like a
        // rendered sprite even though nothing will actually draw there.
        const bool has_tile = layer.animated_cells.contains(cell_idx) ||
                              tiles[row, col] != corundum::gameplay::world::tilemap::k_empty_tile;
        if (!has_tile)
          continue;
        draw_iso_cell(ctx, static_cast<float>(col), static_cast<float>(row), static_cast<float>(v), iso.half_tw,
                      iso.half_th, state.elev_step_px, iso.x_origin, state.canvas.offset_x, state.canvas.offset_y,
                      elevation_fill_color(v), 0);
      }
    }
  }

  void render_elevation_preview(CanvasContext ctx, const EditorState &state) {
    if (state.hover_tile_col < 0 || state.hover_tile_row < 0 || state.map.tilesets.empty())
      return;
    const int dw = effective_diamond_w(state.map);
    const int dh = effective_diamond_h(state.map);
    const auto iso = corundum::core::math::compute_iso_params(dw, dh, state.map.height, state.canvas.scale);
    draw_iso_cell(ctx, static_cast<float>(state.hover_tile_col), static_cast<float>(state.hover_tile_row),
                  static_cast<float>(state.selected_elevation), iso.half_tw, iso.half_th, state.elev_step_px,
                  iso.x_origin, state.canvas.offset_x, state.canvas.offset_y,
                  elevation_fill_color(state.selected_elevation), IM_COL32(255, 220, 80, 220));
  }

} // namespace tools::tilemap
