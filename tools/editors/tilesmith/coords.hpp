#pragma once
#include <algorithm>
#include <cmath>
#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/world/camera.hpp>
#include <corundum/gameplay/world/tilemap/tilemap.hpp>
#include <optional>
#include <span>

// Pure coordinate math — no GLFW, no I/O, no rendering.
// Returns effective isometric world-step width (diamond_w) for a tilemap.
[[nodiscard]] inline int effective_diamond_w(const corundum::gameplay::world::tilemap::Tilemap &map) noexcept {
  return map.diamond_w();
}

[[nodiscard]] inline int effective_diamond_h(const corundum::gameplay::world::tilemap::Tilemap &map) noexcept {
  return map.diamond_h();
}

namespace tools::tilemap {

  // Number of half-diamonds of margin added to the computed virtual canvas
  // extent so the outermost tile diamond footprints aren't clipped by the
  // scroll region edge.
  inline constexpr float k_content_margin = 2.f;

  /**
   * @brief Tile-grid coordinate (column, row).
   */
  struct TileCoord {
    int col;
    int row;
  };

  /**
   * @brief Convert a window pixel position to a map tile coordinate.
   *
   * @param px, py          Window-space pixel position.
   * @param canvas_left     Left edge of the canvas region in window space.
   * @param canvas_top      Top edge of the canvas region in window space.
   * @param canvas_w        Width of the canvas in pixels.
   * @param canvas_h        Height of the canvas in pixels.
   * @param camera_x, camera_y  Current camera scroll offset.
   * @param tile_scale      Display scale factor.
   * @param map_w, map_h    Map dimensions in tiles.
   * @param tw, th          Tile dimensions in Tiled pixel space.
   * @return Tile coordinate, or nullopt if outside the canvas or map bounds.
   */
  [[nodiscard]] inline std::optional<TileCoord> screen_to_tile(int px, int py, int canvas_left, int canvas_top,
                                                               int canvas_w, int canvas_h, float camera_x,
                                                               float camera_y, float tile_scale, int map_w, int map_h,
                                                               int tw, int diamond_h) noexcept {
    if (px < canvas_left || px >= canvas_left + canvas_w)
      return std::nullopt;
    if (py < canvas_top || py >= canvas_top + canvas_h)
      return std::nullopt;

    // tw is the isometric diamond_w (world step), not the sprite cell width.
    const auto iso =
        corundum::core::math::compute_iso_params(tw, diamond_h > 0 ? diamond_h : tw / 2, map_h, tile_scale);
    if (iso.half_tw <= 0.f || iso.half_th <= 0.f)
      return std::nullopt;

    // Undo the origin shift the canvas renderer applies (see main.cpp's comment on the left/right
    // asymmetry) so picking stays aligned with rendering.
    const float origin_shift_x = k_content_margin * iso.half_tw;
    const float origin_shift_y = iso.half_th; // Y shift uses 1 half-diamond (no asymmetry).
    const float world_x = static_cast<float>(px - canvas_left) + camera_x - origin_shift_x;
    const float world_y = static_cast<float>(py - canvas_top) + camera_y - origin_shift_y;
    const corundum::core::math::Vec2 frac =
        corundum::core::math::world_to_tile({world_x, world_y}, 0, iso.half_tw, iso.half_th, 0.f, iso.x_origin);
    const int col = static_cast<int>(std::floor(frac.x));
    const int row = static_cast<int>(std::floor(frac.y));

    if (col < 0 || col >= map_w || row < 0 || row >= map_h)
      return std::nullopt;
    return TileCoord{col, row};
  }

  /**
   * @brief One tile's placement within the palette panel's flow layout, in unscrolled panel-local
   * pixels (top-left origin at the panel's top-left, before palette_scroll_y is subtracted).
   */
  struct PaletteCell {
    int local_id;
    int x, y;
    int w, h;
  };

  /// Padding in pixels between adjacent palette cells, both horizontally and between rows.
  inline constexpr int k_palette_cell_padding = 2;

  /**
   * @brief Lays out every tile in @p ts as a left-to-right, top-to-bottom flow (wrapping to a new
   * row when a tile would exceed @p available_w), since a MaxRects-packed tileset has no uniform
   * cell size or column count to lay out as a fixed grid. Each row's height is its tallest tile.
   *
   * Shared between rendering (render_tile_grid.cpp) and click hit-testing (this function's
   * palette_click_to_gid below), so the two can never drift out of sync with each other.
   *
   * @param ts          Tileset whose tiles are displayed in the palette.
   * @param available_w Panel width in pixels to wrap within.
   * @param tile_scale  Display scale factor.
   * @return One PaletteCell per tile, in local_id order.
   */
  [[nodiscard]] inline std::vector<PaletteCell>
  compute_palette_layout(const corundum::gameplay::world::tilemap::TilemapTileset &ts, int available_w,
                         float tile_scale) {
    std::vector<PaletteCell> cells;
    cells.reserve(static_cast<std::size_t>(std::max(0, ts.tile_count)));
    int cursor_x = 0, cursor_y = 0, row_h = 0;
    for (int i = 0; i < ts.tile_count; ++i) {
      const auto &rect = ts.info.tile_rects[static_cast<std::size_t>(i)];
      const int w = std::max(1, static_cast<int>(static_cast<float>(rect.width) * tile_scale));
      const int h = std::max(1, static_cast<int>(static_cast<float>(rect.height) * tile_scale));
      if (cursor_x > 0 && cursor_x + w > available_w) {
        cursor_x = 0;
        cursor_y += row_h + k_palette_cell_padding;
        row_h = 0;
      }
      cells.push_back({i, cursor_x, cursor_y, w, h});
      cursor_x += w + k_palette_cell_padding;
      row_h = std::max(row_h, h);
    }
    return cells;
  }

  /**
   * @brief Convert a palette panel click to an absolute tile GID, using the same flow layout
   * compute_palette_layout() produces (and render_tile_grid.cpp draws).
   *
   * Callers should compute the layout once (via compute_palette_layout()) and pass it in via @p cells
   * rather than recomputing on every click.
   *
   * @param panel_local_x, panel_local_y  Position relative to the top-left of the palette panel.
   * @param ts            Tileset whose tiles are displayed in the panel.
   * @param scroll_y      Vertical scroll offset in pixels (EditorState::palette_scroll_y).
   * @param cells         Precomputed palette layout — see compute_palette_layout().
   * @return Absolute GID, or nullopt if the click doesn't land on a tile.
   */
  [[nodiscard]] inline std::optional<corundum::gameplay::world::tilemap::TileId>
  palette_click_to_gid(int panel_local_x, int panel_local_y,
                       const corundum::gameplay::world::tilemap::TilemapTileset &ts, float scroll_y,
                       std::span<const PaletteCell> cells) noexcept {
    const int content_y = panel_local_y + static_cast<int>(scroll_y);
    for (const auto &cell : cells) {
      if (panel_local_x >= cell.x && panel_local_x < cell.x + cell.w && content_y >= cell.y &&
          content_y < cell.y + cell.h)
        return static_cast<corundum::gameplay::world::tilemap::TileId>(static_cast<int>(ts.first_gid) + cell.local_id);
    }
    return std::nullopt;
  }

  /**
   * @brief Convert two tile-grid corners to a CollisionRect in Tiled pixel space.
   *
   * Normalizes drag direction so w/h are always positive. A single-tile click
   * (col_a == col_b, row_a == row_b) produces a rect exactly one tile wide/tall.
   *
   * @param col_a, row_a  Drag anchor tile coordinate.
   * @param col_b, row_b  Current (or release) tile coordinate.
   * @param tile_w, tile_h  Tile dimensions in Tiled pixel space.
   * @return CollisionRect with positive w/h.
   */
  [[nodiscard]] inline corundum::gameplay::world::tilemap::CollisionRect
  snap_to_tile_rect(int col_a, int row_a, int col_b, int row_b) noexcept {
    const int min_col = std::min(col_a, col_b);
    const int min_row = std::min(row_a, row_b);
    const int max_col = std::max(col_a, col_b);
    const int max_row = std::max(row_a, row_b);
    return {static_cast<float>(min_col), static_cast<float>(min_row), static_cast<float>(max_col - min_col + 1),
            static_cast<float>(max_row - min_row + 1)};
  }

  /**
   * @brief Convert two window pixel drag positions to a CollisionRect in tile-grid space.
   *
   * Used for sub-tile precision collision rect placement (Shift-drag). Coordinates are
   * converted from window space to tile-grid space via camera and tile_scale, then normalized
   * to a positive-size rect.
   *
   * @param win_x_a, win_y_a  Window-space position at drag anchor.
   * @param win_x_b, win_y_b  Window-space position at drag release (or current cursor).
   * @param camera_x, camera_y  Current camera scroll offset.
   * @param tile_scale  Display scale factor.
   * @param tile_w, tile_h  Tile dimensions in pixels.
   * @return CollisionRect in tile-grid space with col_span/row_span >= 1/tile_w, 1/tile_h.
   */
  [[nodiscard]] inline corundum::gameplay::world::tilemap::CollisionRect
  pixel_to_tiled_rect(int win_x_a, int win_y_a, int win_x_b, int win_y_b, float camera_x, float camera_y,
                      float tile_scale, float tile_w, float tile_h) noexcept {
    const float txa = (static_cast<float>(win_x_a) + camera_x) / tile_scale;
    const float tya = (static_cast<float>(win_y_a) + camera_y) / tile_scale;
    const float txb = (static_cast<float>(win_x_b) + camera_x) / tile_scale;
    const float tyb = (static_cast<float>(win_y_b) + camera_y) / tile_scale;
    const float min_col = std::min(txa, txb) / tile_w;
    const float min_row = std::min(tya, tyb) / tile_h;
    const float max_col = std::max(txa, txb) / tile_w;
    const float max_row = std::max(tya, tyb) / tile_h;
    return {min_col, min_row, std::max(1.f / tile_w, max_col - min_col), std::max(1.f / tile_h, max_row - min_row)};
  }

  /**
   * @brief Clamp a camera position so it cannot scroll past the map boundary.
   *
   * Allows negative values so maps smaller than the canvas can be centered.
   *
   * @param cam        Camera to clamp (passed by value).
   * @param tile_scale Display scale factor.
   * @param map_w      Map width in tiles.
   * @param map_h      Map height in tiles.
   * @param tw         Tile width in pixels.
   * @param th         Tile height in pixels.
   * @param canvas_w   Canvas width in pixels.
   * @param canvas_h   Canvas height in pixels.
   * @return Clamped camera.
   */
  [[nodiscard]] inline corundum::gameplay::world::Camera clamp_camera(corundum::gameplay::world::Camera cam,
                                                                      float tile_scale, int map_w, int map_h, int tw,
                                                                      int diamond_h, int canvas_w,
                                                                      int canvas_h) noexcept {
    const float half_tw = static_cast<float>(tw) * tile_scale * 0.5f;
    const float half_th = static_cast<float>(diamond_h > 0 ? diamond_h : tw / 2) * tile_scale * 0.5f;
    // +k_content_margin half-diamonds: matches the margin added by the canvas renderer/content-size
    // calc (main.cpp) — keeps scroll clamping consistent with that padding.
    const float map_px_w = static_cast<float>(map_w + map_h) * half_tw + k_content_margin * half_tw;
    const float map_px_h = static_cast<float>(map_w + map_h) * half_th + k_content_margin * half_th;
    const float min_x = std::min(0.f, (map_px_w - static_cast<float>(canvas_w)) * 0.5f);
    const float min_y = std::min(0.f, (map_px_h - static_cast<float>(canvas_h)) * 0.5f);
    cam.x = std::clamp(cam.x, min_x, std::max(0.f, map_px_w - static_cast<float>(canvas_w)));
    cam.y = std::clamp(cam.y, min_y, std::max(0.f, map_px_h - static_cast<float>(canvas_h)));
    return cam;
  }

} // namespace tools::tilemap
