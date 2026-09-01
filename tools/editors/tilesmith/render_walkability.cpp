#include "render_walkability.hpp"
#include "coords.hpp"
#include <algorithm>
#include <corundum/world/tilemap/walkability.hpp>

namespace tools::tilesmith {

  namespace {

    // Convert tile-grid coords (col, row) to canvas screen position, lifted by
    // elevation — same convention as render_elevation.cpp's elev_tile_to_iso.
    // Delegates the iso projection to corundum::core::math::tile_to_world.
    inline ImVec2 walk_tile_to_iso(const CanvasContext &ctx, float col_f, float row_f, float elev,
                                   const corundum::core::math::IsometricParams &iso, float offset_x,
                                   float offset_y) noexcept {
      const auto w = corundum::core::math::tile_to_world(col_f, row_f, elev, iso);
      return {ctx.origin.x + w.x - offset_x, ctx.origin.y + w.y - offset_y};
    }

  } // namespace

  void render_walkability_overlay(CanvasContext ctx, const EditorState &state) {
    if (state.map.tilesets.empty())
      return;

    using corundum::world::tilemap::build_walkability_graph;
    using corundum::world::tilemap::elevation_at;
    using corundum::world::tilemap::WalkabilityGraph;

    const WalkabilityGraph graph = build_walkability_graph(state.map, static_cast<int>(state.max_step_height));

    const int dw = effective_diamond_w(state.map);
    const int dh = effective_diamond_h(state.map);
    const auto iso = corundum::core::math::compute_isometric_params(dw, dh, state.map.height, state.canvas.scale,
                                                                    state.elev_step_px);

    constexpr ImU32 k_color = IM_COL32(255, 0, 200, 220);

    for (int row = 0; row < state.map.height; ++row) {
      for (int col = 0; col < state.map.width; ++col) {
        // East edge: shared boundary is the rhombus's right->bottom side.
        if (col + 1 < state.map.width && !graph.can_move(col, row, col + 1, row)) {
          const float elev =
              static_cast<float>(std::max(elevation_at(state.map, col, row), elevation_at(state.map, col + 1, row)));
          const ImVec2 p0 = walk_tile_to_iso(ctx, static_cast<float>(col + 1), static_cast<float>(row), elev, iso,
                                             state.canvas.offset_x, state.canvas.offset_y);
          const ImVec2 p1 = walk_tile_to_iso(ctx, static_cast<float>(col + 1), static_cast<float>(row + 1), elev, iso,
                                             state.canvas.offset_x, state.canvas.offset_y);
          ctx.dl->AddLine(p0, p1, k_color, 3.f);
        }

        // South edge: shared boundary is the rhombus's bottom->left side.
        if (row + 1 < state.map.height && !graph.can_move(col, row, col, row + 1)) {
          const float elev =
              static_cast<float>(std::max(elevation_at(state.map, col, row), elevation_at(state.map, col, row + 1)));
          const ImVec2 p0 = walk_tile_to_iso(ctx, static_cast<float>(col + 1), static_cast<float>(row + 1), elev, iso,
                                             state.canvas.offset_x, state.canvas.offset_y);
          const ImVec2 p1 = walk_tile_to_iso(ctx, static_cast<float>(col), static_cast<float>(row + 1), elev, iso,
                                             state.canvas.offset_x, state.canvas.offset_y);
          ctx.dl->AddLine(p0, p1, k_color, 3.f);
        }
      }
    }
  }

} // namespace tools::tilesmith
