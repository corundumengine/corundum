// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include "render_collision.hpp"
#include "coords.hpp"
#include "editor_state.hpp"
#include "layout.hpp"
#include <corundum/core/math/isometric.hpp>
#include <corundum/world/tilemap/tilemap.hpp>
#include <cstddef>
#include <imgui.h>

namespace tools::tilesmith {

  namespace {

    /// The projection, Tiled-pixel cell size and camera offset the collision overlays all draw with.
    struct CollisionGeometry {
      corundum::core::math::IsometricParams iso{};

      float tile_w = 0.f;

      float tile_h = 0.f;

      float inv_tw = 0.f;

      float inv_th = 0.f;

      float offset_x = 0.f;

      float offset_y = 0.f;
    };

    [[nodiscard]] CollisionGeometry collision_geometry(const EditorState &state) noexcept {
      const float tile_w = static_cast<float>(state.map.diamond_w());
      const float tile_h = static_cast<float>(state.map.diamond_h());
      return {
          .iso = corundum::core::math::compute_isometric_params(effective_diamond_w(state.map),
                                                                effective_diamond_h(state.map), state.map.height,
                                                                state.canvas.scale, state.elev_step_px),
          .tile_w = tile_w,
          .tile_h = tile_h,
          .inv_tw = 1.f / tile_w,
          .inv_th = 1.f / tile_h,
          .offset_x = state.canvas.offset_x,
          .offset_y = state.canvas.offset_y,
      };
    }

    // Convert a Tiled-pixel position (x_px, y_px) to canvas screen space, lifted by
    // elevation so a shape authored on a raised platform renders sitting on it.
    // x_px = col * tile_w, y_px = row * tile_h (sub-tile fractions allowed).
    // Delegates the iso projection to corundum::core::math::tile_to_world.
    inline ImVec2 tiled_to_iso(const CanvasContext &ctx, const CollisionGeometry &geometry, float x_px, float y_px,
                               int elevation) noexcept {
      const float col_f = x_px * geometry.inv_tw;
      const float row_f = y_px * geometry.inv_th;
      const auto world = corundum::core::math::tile_to_world(col_f, row_f, elevation, geometry.iso);
      return {ctx.origin.x + world.x - geometry.offset_x, ctx.origin.y + world.y - geometry.offset_y};
    }

    // Draw a tile-grid rect as an isometric rhombus (4-vertex quad).
    void draw_iso_rect(const CanvasContext &ctx, const CollisionGeometry &geometry, float col, float row,
                       float col_span, float row_span, int elevation, ImU32 fill, ImU32 outline) noexcept {
      const float x = col * geometry.tile_w;
      const float y = row * geometry.tile_h;
      const float w = col_span * geometry.tile_w;
      const float h = row_span * geometry.tile_h;
      const ImVec2 top = tiled_to_iso(ctx, geometry, x, y, elevation);
      const ImVec2 right = tiled_to_iso(ctx, geometry, x + w, y, elevation);
      const ImVec2 bottom = tiled_to_iso(ctx, geometry, x + w, y + h, elevation);
      const ImVec2 left = tiled_to_iso(ctx, geometry, x, y + h, elevation);
      ctx.dl->AddQuadFilled(top, right, bottom, left, fill);
      ctx.dl->AddQuad(top, right, bottom, left, outline, 1.f);
    }

    // Draw only the solid half of a tile for a given TriangleCut. The rhombus's four
    // vertices are top=NW corner, right=NE corner, bottom=SE corner, left=SW corner
    // (per tiled_to_iso's corner mapping); TriangleCut names the EMPTY corner, so the
    // solid triangle is the other three vertices.
    void draw_iso_triangle(const CanvasContext &ctx, const CollisionGeometry &geometry, float col, float row,
                           float col_span, float row_span, int elevation, corundum::world::tilemap::TriangleCut cut,
                           ImU32 fill, ImU32 outline) noexcept {
      using corundum::world::tilemap::TriangleCut;
      const float x = col * geometry.tile_w;
      const float y = row * geometry.tile_h;
      const float w = col_span * geometry.tile_w;
      const float h = row_span * geometry.tile_h;
      const ImVec2 top = tiled_to_iso(ctx, geometry, x, y, elevation);
      const ImVec2 right = tiled_to_iso(ctx, geometry, x + w, y, elevation);
      const ImVec2 bottom = tiled_to_iso(ctx, geometry, x + w, y + h, elevation);
      const ImVec2 left = tiled_to_iso(ctx, geometry, x, y + h, elevation);

      ImVec2 a;
      ImVec2 b;
      ImVec2 c;
      switch (cut) {
        case TriangleCut::NorthWest:
          a = right;
          b = bottom;
          c = left;
          break;
        case TriangleCut::SouthEast:
          a = top;
          b = right;
          c = left;
          break;
        case TriangleCut::NorthEast:
          a = top;
          b = bottom;
          c = left;
          break;
        case TriangleCut::SouthWest:
          a = top;
          b = right;
          c = bottom;
          break;
      }
      ctx.dl->AddTriangleFilled(a, b, c, fill);
      ctx.dl->AddTriangle(a, b, c, outline, 1.f);
    }

  } // namespace

  void render_collisions(CanvasContext ctx, const EditorState &state) {
    if (state.map.tilesets.empty())
      return;
    const CollisionGeometry geometry = collision_geometry(state);

    const auto &cols = state.map.collisions;
    for (std::size_t i = 0; i < cols.size(); ++i) {
      draw_iso_rect(ctx, geometry, cols.cols[i], cols.rows[i], cols.col_spans[i], cols.row_spans[i], cols.elevations[i],
                    IM_COL32(255, 80, 80, 60), IM_COL32(255, 80, 80, 200));
    }

    const auto &tris = state.map.collision_triangles;
    for (std::size_t i = 0; i < tris.size(); ++i) {
      draw_iso_triangle(ctx, geometry, tris.cols[i], tris.rows[i], tris.col_spans[i], tris.row_spans[i],
                        tris.elevations[i], tris.cuts[i], IM_COL32(255, 140, 60, 70), IM_COL32(255, 140, 60, 210));
    }

    if (state.show_collisions && state.triangle_collision_mode && state.hover_tile_col >= 0 &&
        state.hover_tile_row >= 0) {
      // Skip the placement preview if a triangle already occupies the hovered cell — drawing a
      // ghost triangle in the same spot hides the deletion feedback (right-click erases the orange
      // one, but the green preview immediately takes its place, making it look like nothing
      // happened).
      const float hover_col_f = static_cast<float>(state.hover_tile_col);
      const float hover_row_f = static_cast<float>(state.hover_tile_row);
      bool occupied = false;
      for (std::size_t ti = 0; ti < tris.size(); ++ti) {
        if (tris.cols[ti] == hover_col_f && tris.rows[ti] == hover_row_f && tris.col_spans[ti] == 1.f &&
            tris.row_spans[ti] == 1.f) {
          occupied = true;
          break;
        }
      }
      if (!occupied) {
        // Preview at the elevation this triangle would actually be authored at if placed now.
        const int preview_elevation =
            corundum::world::tilemap::elevation_at(state.map, state.hover_tile_col, state.hover_tile_row);
        draw_iso_triangle(ctx, geometry, hover_col_f, hover_row_f, 1.f, 1.f, preview_elevation, state.collision_tri_cut,
                          IM_COL32(100, 255, 100, 50), IM_COL32(100, 255, 100, 220));
      }
    }
  }

  void render_collision_preview(CanvasContext ctx, const EditorState &state) {
    if (!state.collision_dragging || state.map.tilesets.empty())
      return;
    const CollisionGeometry geometry = collision_geometry(state);

    corundum::world::tilemap::CollisionRect cr;
    if (state.col_drag_sub_tile) {
      cr = pixel_to_tiled_rect(state.col_drag_anchor_win_x, state.col_drag_anchor_win_y, state.col_drag_cur_win_x,
                               state.col_drag_cur_win_y, 0, k_menu_h, CANVAS_W, CANVAS_H, state.canvas.offset_x,
                               state.canvas.offset_y, state.canvas.scale, state.elev_step_px, state.map.height,
                               state.map.diamond_w(), state.map.diamond_h(), state.map, state.layer_visible);
    } else {
      cr = snap_to_tile_rect(state.col_drag_anchor_col, state.col_drag_anchor_row, state.col_drag_cur_col,
                             state.col_drag_cur_row);
    }
    // Preview at the elevation this rect would actually be authored at if committed now.
    const int preview_elevation =
        corundum::world::tilemap::elevation_at(state.map, static_cast<int>(cr.col), static_cast<int>(cr.row));
    draw_iso_rect(ctx, geometry, cr.col, cr.row, cr.col_span, cr.row_span, preview_elevation,
                  IM_COL32(100, 255, 100, 40), IM_COL32(100, 255, 100, 230));
  }

  void render_erase_preview(CanvasContext ctx, const EditorState &state) {
    if (!state.erase_dragging || state.map.tilesets.empty())
      return;
    const CollisionGeometry geometry = collision_geometry(state);

    const auto cr = snap_to_tile_rect(state.erase_drag_anchor_col, state.erase_drag_anchor_row,
                                      state.erase_drag_cur_col, state.erase_drag_cur_row);
    draw_iso_rect(ctx, geometry, cr.col, cr.row, cr.col_span, cr.row_span, 0, IM_COL32(255, 100, 60, 50),
                  IM_COL32(255, 100, 60, 220));
  }

} // namespace tools::tilesmith
