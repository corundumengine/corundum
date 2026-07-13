#include "render_ramps.hpp"
#include "coords.hpp"

namespace tools::tilemap {

  namespace {

    using corundum::gameplay::world::tilemap::elevation_at;
    using corundum::gameplay::world::tilemap::RampAxis;

    // Convert tile-grid coords (col, row) to isometric screen position, lifted by
    // elevation — same convention as render_elevation.cpp's elev_tile_to_iso.
    inline ImVec2 ramp_tile_to_iso(const CanvasContext &ctx, float col_f, float row_f, float elev, float half_tw,
                                   float half_th, float elev_step, float x_shift, float cam_x, float cam_y) noexcept {
      return {ctx.origin.x + (col_f - row_f) * half_tw + x_shift - cam_x,
              ctx.origin.y + (col_f + row_f) * half_th - elev * elev_step - cam_y};
    }

    void draw_ramp_line(const CanvasContext &ctx, const EditorState &state, int col, int row, RampAxis axis,
                        const corundum::core::math::IsoParams &iso, ImU32 color) {
      const float elev = static_cast<float>(elevation_at(state.map, col, row));
      const auto [dc, dr] = axis == RampAxis::NORTH_SOUTH ? std::pair{0.f, -0.5f} : std::pair{0.5f, 0.f};
      const ImVec2 p0 =
          ramp_tile_to_iso(ctx, static_cast<float>(col) + 0.5f - dc, static_cast<float>(row) + 0.5f - dr, elev,
                           iso.half_tw, iso.half_th, state.elev_step_px, iso.x_origin, state.camera.x, state.camera.y);
      const ImVec2 p1 =
          ramp_tile_to_iso(ctx, static_cast<float>(col) + 0.5f + dc, static_cast<float>(row) + 0.5f + dr, elev,
                           iso.half_tw, iso.half_th, state.elev_step_px, iso.x_origin, state.camera.x, state.camera.y);
      ctx.dl->AddLine(p0, p1, color, 3.f);
      ctx.dl->AddCircleFilled(p1, 4.f, color);
    }

  } // namespace

  void render_ramps_overlay(CanvasContext ctx, const EditorState &state) {
    if (state.map.tilesets.empty() || state.active_layer >= static_cast<int>(state.map.layers.size()))
      return;
    const auto &layer = state.map.layers[static_cast<std::size_t>(state.active_layer)];
    if (layer.ramps.empty())
      return;

    const int dw = effective_diamond_w(state.map);
    const int dh = effective_diamond_h(state.map);
    const auto iso = corundum::core::math::compute_iso_params(dw, dh, state.map.height, state.tile_scale);

    constexpr ImU32 k_color = IM_COL32(120, 255, 120, 230);
    for (const auto &[idx, axis] : layer.ramps)
      draw_ramp_line(ctx, state, idx % state.map.width, idx / state.map.width, axis, iso, k_color);
  }

  void render_ramp_preview(CanvasContext ctx, const EditorState &state) {
    if (state.hover_tile_col < 0 || state.hover_tile_row < 0 || state.map.tilesets.empty())
      return;
    const int dw = effective_diamond_w(state.map);
    const int dh = effective_diamond_h(state.map);
    const auto iso = corundum::core::math::compute_iso_params(dw, dh, state.map.height, state.tile_scale);
    constexpr ImU32 k_color = IM_COL32(255, 220, 80, 220);
    draw_ramp_line(ctx, state, state.hover_tile_col, state.hover_tile_row, state.selected_ramp_axis, iso, k_color);
  }

} // namespace tools::tilemap
