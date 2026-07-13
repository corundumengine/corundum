#include "render_walkability.hpp"
#include "coords.hpp"
#include <algorithm>
#include <corundum/gameplay/world/tilemap/walkability.hpp>

namespace tools::tilemap {

  namespace {

    // Convert tile-grid coords (col, row) to isometric screen position, lifted by
    // elevation — same convention as render_elevation.cpp's elev_tile_to_iso.
    inline ImVec2 walk_tile_to_iso(const CanvasContext &ctx, float col_f, float row_f, float elev, float half_tw,
                                   float half_th, float elev_step, float x_shift, float cam_x, float cam_y) noexcept {
      return {ctx.origin.x + (col_f - row_f) * half_tw + x_shift - cam_x,
              ctx.origin.y + (col_f + row_f) * half_th - elev * elev_step - cam_y};
    }

  } // namespace

  void render_walkability_overlay(CanvasContext ctx, const EditorState &state) {
    if (state.map.tilesets.empty())
      return;

    using corundum::gameplay::world::tilemap::build_walkability_graph;
    using corundum::gameplay::world::tilemap::elevation_at;
    using corundum::gameplay::world::tilemap::WalkabilityGraph;

    const WalkabilityGraph graph = build_walkability_graph(state.map, static_cast<int>(state.max_step_height));

    const int dw = effective_diamond_w(state.map);
    const int dh = effective_diamond_h(state.map);
    const auto iso = corundum::core::math::compute_iso_params(dw, dh, state.map.height, state.canvas.scale);

    constexpr ImU32 k_color = IM_COL32(255, 0, 200, 220);

    for (int row = 0; row < state.map.height; ++row) {
      for (int col = 0; col < state.map.width; ++col) {
        // East edge: shared boundary is the rhombus's right->bottom side.
        if (col + 1 < state.map.width && !graph.can_move(col, row, col + 1, row)) {
          const float elev =
              static_cast<float>(std::max(elevation_at(state.map, col, row), elevation_at(state.map, col + 1, row)));
          const ImVec2 p0 = walk_tile_to_iso(ctx, static_cast<float>(col + 1), static_cast<float>(row), elev,
                                             iso.half_tw, iso.half_th, state.elev_step_px, iso.x_origin,
                                             state.canvas.offset_x, state.canvas.offset_y);
          const ImVec2 p1 = walk_tile_to_iso(ctx, static_cast<float>(col + 1), static_cast<float>(row + 1), elev,
                                             iso.half_tw, iso.half_th, state.elev_step_px, iso.x_origin,
                                             state.canvas.offset_x, state.canvas.offset_y);
          ctx.dl->AddLine(p0, p1, k_color, 3.f);
        }

        // South edge: shared boundary is the rhombus's bottom->left side.
        if (row + 1 < state.map.height && !graph.can_move(col, row, col, row + 1)) {
          const float elev =
              static_cast<float>(std::max(elevation_at(state.map, col, row), elevation_at(state.map, col, row + 1)));
          const ImVec2 p0 = walk_tile_to_iso(ctx, static_cast<float>(col + 1), static_cast<float>(row + 1), elev,
                                             iso.half_tw, iso.half_th, state.elev_step_px, iso.x_origin,
                                             state.canvas.offset_x, state.canvas.offset_y);
          const ImVec2 p1 = walk_tile_to_iso(ctx, static_cast<float>(col), static_cast<float>(row + 1), elev,
                                             iso.half_tw, iso.half_th, state.elev_step_px, iso.x_origin,
                                             state.canvas.offset_x, state.canvas.offset_y);
          ctx.dl->AddLine(p0, p1, k_color, 3.f);
        }
      }
    }
  }

} // namespace tools::tilemap
