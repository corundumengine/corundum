#include "render_ramps.hpp"
#include "coords.hpp"

namespace tools::tilesmith {

  namespace {

    using corundum::world::tilemap::elevation_at;
    using corundum::world::tilemap::RampAxis;

    // Convert tile-grid coords (col, row) to canvas screen position, lifted by
    // elevation. Delegates the iso projection to corundum::core::math::tile_to_world
    // so this stays in lockstep with engine entity/camera sites — kills one of the
    // five tilesmith inline projection copies called out in the audit.
    inline ImVec2 ramp_tile_to_iso(const CanvasContext &ctx, float col_f, float row_f, float elev,
                                   const corundum::core::math::IsometricParams &iso, float offset_x,
                                   float offset_y) noexcept {
      const auto w = corundum::core::math::tile_to_world(col_f, row_f, elev, iso);
      return {ctx.origin.x + w.x - offset_x, ctx.origin.y + w.y - offset_y};
    }

    void draw_ramp_line(const CanvasContext &ctx, const EditorState &state, int col, int row, RampAxis axis,
                        const corundum::core::math::IsometricParams &iso, ImU32 color) {
      const float elev = static_cast<float>(elevation_at(state.map, col, row));
      const auto [dc, dr] = axis == RampAxis::NorthSouth ? std::pair{0.f, -0.5f} : std::pair{0.5f, 0.f};
      const ImVec2 p0 = ramp_tile_to_iso(ctx, static_cast<float>(col) + 0.5f - dc, static_cast<float>(row) + 0.5f - dr,
                                         elev, iso, state.canvas.offset_x, state.canvas.offset_y);
      const ImVec2 p1 = ramp_tile_to_iso(ctx, static_cast<float>(col) + 0.5f + dc, static_cast<float>(row) + 0.5f + dr,
                                         elev, iso, state.canvas.offset_x, state.canvas.offset_y);
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
    const auto iso = corundum::core::math::compute_isometric_params(dw, dh, state.map.height, state.canvas.scale,
                                                                    state.elev_step_px);

    constexpr ImU32 k_color = IM_COL32(120, 255, 120, 230);
    for (const auto &[idx, axis] : layer.ramps)
      draw_ramp_line(ctx, state, idx % state.map.width, idx / state.map.width, axis, iso, k_color);
  }

  void render_ramp_preview(CanvasContext ctx, const EditorState &state) {
    if (state.hover_tile_col < 0 || state.hover_tile_row < 0 || state.map.tilesets.empty())
      return;
    const int dw = effective_diamond_w(state.map);
    const int dh = effective_diamond_h(state.map);
    const auto iso = corundum::core::math::compute_isometric_params(dw, dh, state.map.height, state.canvas.scale,
                                                                    state.elev_step_px);
    constexpr ImU32 k_color = IM_COL32(255, 220, 80, 220);
    draw_ramp_line(ctx, state, state.hover_tile_col, state.hover_tile_row, state.selected_ramp_axis, iso, k_color);
  }

} // namespace tools::tilesmith
