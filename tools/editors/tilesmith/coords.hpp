#pragma once
#include <algorithm>
#include <cmath>
#include <corundum/gameplay/world/camera.hpp>
#include <corundum/gameplay/world/tilemap/tilemap.hpp>
#include <optional>

// Pure coordinate math — no GLFW, no I/O, no rendering.
// Returns effective isometric world-step width (diamond_w) for a tilemap.
[[nodiscard]] inline int effective_diamond_w(const corundum::gameplay::world::tilemap::Tilemap &map) noexcept {
  return map.diamond_w();
}

[[nodiscard]] inline int effective_diamond_h(const corundum::gameplay::world::tilemap::Tilemap &map) noexcept {
  return map.diamond_h();
}

namespace tools::tilemap {

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
    const float half_tw = static_cast<float>(tw) * tile_scale * 0.5f;
    const float half_th = static_cast<float>(diamond_h > 0 ? diamond_h : tw / 2) * tile_scale * 0.5f;
    if (half_tw <= 0.f || half_th <= 0.f)
      return std::nullopt;

    // Isometric inverse: adj_iso_x = (col - row + map_h - 1) * half_tw
    //                    adj_iso_y = (col + row) * half_th
    const float world_x = static_cast<float>(px - canvas_left) + camera_x;
    const float world_y = static_cast<float>(py - canvas_top) + camera_y;
    const float u = world_x / half_tw; // col - row + map_h
    const float v = world_y / half_th; // col + row
    const float h1 = static_cast<float>(map_h - 1);
    const int col = static_cast<int>(std::floor((u + v - h1) * 0.5f));
    const int row = static_cast<int>(std::floor((v - u + h1) * 0.5f));

    if (col < 0 || col >= map_w || row < 0 || row >= map_h)
      return std::nullopt;
    return TileCoord{col, row};
  }

  /**
   * @brief Convert a palette tile-grid click to an absolute tile GID.
   *
   * The palette grid mirrors the tileset image directly: column @p scroll_col is
   * the left-most visible column and @p scroll_row is the top-most visible row.
   *
   * @param panel_local_x, panel_local_y  Position relative to the top-left of the tile grid.
   * @param ts          Tileset whose tiles are displayed in the grid.
   * @param scroll_row  First visible row in the tile grid.
   * @param scroll_col  First visible column in the tile grid.
   * @param tile_scale  Display scale factor.
   * @return Absolute GID, or nullopt if the click is out of bounds.
   */
  [[nodiscard]] inline std::optional<corundum::gameplay::world::tilemap::TileId>
  palette_click_to_gid(int panel_local_x, int panel_local_y,
                       const corundum::gameplay::world::tilemap::TilemapTileset &ts, int scroll_row, int scroll_col,
                       float tile_scale) noexcept {
    const int cell_w = std::max(1, static_cast<int>(static_cast<float>(ts.info.frame_width) * tile_scale));
    const int cell_h = std::max(1, static_cast<int>(static_cast<float>(ts.info.frame_height) * tile_scale));

    const int actual_col = panel_local_x / cell_w + scroll_col;
    const int actual_row = panel_local_y / cell_h + scroll_row;

    if (actual_col < 0 || actual_col >= ts.info.columns || actual_row < 0)
      return std::nullopt;

    const int local_id = actual_row * ts.info.columns + actual_col;
    if (local_id < 0 || local_id >= ts.tile_count)
      return std::nullopt;

    return static_cast<corundum::gameplay::world::tilemap::TileId>(static_cast<int>(ts.first_gid) + local_id);
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
    const float map_px_w = static_cast<float>(map_w + map_h) * half_tw;
    const float map_px_h = static_cast<float>(map_w + map_h) * half_th;
    const float min_x = std::min(0.f, (map_px_w - static_cast<float>(canvas_w)) * 0.5f);
    const float min_y = std::min(0.f, (map_px_h - static_cast<float>(canvas_h)) * 0.5f);
    cam.x = std::clamp(cam.x, min_x, std::max(0.f, map_px_w - static_cast<float>(canvas_w)));
    cam.y = std::clamp(cam.y, min_y, std::max(0.f, map_px_h - static_cast<float>(canvas_h)));
    return cam;
  }

} // namespace tools::tilemap
