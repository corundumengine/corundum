#pragma once
#include <algorithm>
#include <corundum/resources/sprite.hpp>
#include <optional>
#include <utility>

// Pure coordinate math — no GLFW, no I/O, no rendering.

namespace tools::sprite {

  /**
   * @brief Canvas-space bounding rectangle for a single frame cell.
   */
  struct FrameRect {
    float x; ///< Left edge in canvas pixels.
    float y; ///< Top edge in canvas pixels.
    float w; ///< Width in canvas pixels.
    float h; ///< Height in canvas pixels.
  };

  /**
   * @brief Convert a canvas pixel position to a frame grid coordinate.
   *
   * @param px, py         Canvas-space pixel position.
   * @param canvas_w       Canvas width in pixels.
   * @param canvas_h       Canvas height in pixels.
   * @param camera_x       Horizontal scroll offset.
   * @param camera_y       Vertical scroll offset.
   * @param zoom           Display scale factor.
   * @param frame_w        Frame cell width in image pixels.
   * @param frame_h        Frame cell height in image pixels.
   * @param offset_x       Left-edge pixel offset to the first frame.
   * @param offset_y       Top-edge pixel offset to the first frame.
   * @param spacing_x      Horizontal gap between cells in image pixels.
   * @param spacing_y      Vertical gap between cells in image pixels.
   * @param img_cols       Total number of frame columns in the sheet.
   * @param img_rows       Total number of frame rows in the sheet.
   * @return Frame coordinate on success; nullopt if outside bounds or in spacing.
   */
  [[nodiscard]] inline std::optional<corundum::resources::FrameCoord>
  screen_to_frame(int px, int py, int canvas_w, int canvas_h, float camera_x, float camera_y, float zoom, int frame_w,
                  int frame_h, int offset_x, int offset_y, int spacing_x, int spacing_y, int img_cols,
                  int img_rows) noexcept {
    if (px < 0 || px >= canvas_w || py < 0 || py >= canvas_h)
      return std::nullopt;
    if (frame_w <= 0 || frame_h <= 0 || img_cols <= 0 || img_rows <= 0)
      return std::nullopt;

    const float wx = (static_cast<float>(px) + camera_x) / zoom;
    const float wy = (static_cast<float>(py) + camera_y) / zoom;
    const float rx = wx - static_cast<float>(offset_x);
    const float ry = wy - static_cast<float>(offset_y);
    if (rx < 0.f || ry < 0.f)
      return std::nullopt;

    const float cell_w = static_cast<float>(frame_w + spacing_x);
    const float cell_h = static_cast<float>(frame_h + spacing_y);
    const int col = static_cast<int>(rx / cell_w);
    const int row = static_cast<int>(ry / cell_h);

    // Reject clicks that land inside the inter-frame spacing gap.
    const float lx = rx - static_cast<float>(col) * cell_w;
    const float ly = ry - static_cast<float>(row) * cell_h;
    if (lx >= static_cast<float>(frame_w) || ly >= static_cast<float>(frame_h))
      return std::nullopt;

    if (col < 0 || col >= img_cols || row < 0 || row >= img_rows)
      return std::nullopt;
    return corundum::resources::FrameCoord{col, row};
  }

  /**
   * @brief Compute the canvas-space rectangle occupied by a frame cell.
   *
   * @param col       Frame column index.
   * @param row       Frame row index.
   * @param camera_x  Horizontal scroll offset.
   * @param camera_y  Vertical scroll offset.
   * @param zoom      Display scale factor.
   * @param frame_w   Frame cell width in image pixels.
   * @param frame_h   Frame cell height in image pixels.
   * @param offset_x  Left-edge pixel offset to the first frame.
   * @param offset_y  Top-edge pixel offset to the first frame.
   * @param spacing_x Horizontal gap between cells in image pixels.
   * @param spacing_y Vertical gap between cells in image pixels.
   * @return Canvas-space FrameRect.
   */
  [[nodiscard]] inline FrameRect frame_to_canvas_rect(int col, int row, float camera_x, float camera_y, float zoom,
                                                      int frame_w, int frame_h, int offset_x, int offset_y,
                                                      int spacing_x, int spacing_y) noexcept {
    const float x = static_cast<float>(offset_x + col * (frame_w + spacing_x)) * zoom - camera_x;
    const float y = static_cast<float>(offset_y + row * (frame_h + spacing_y)) * zoom - camera_y;
    return {x, y, static_cast<float>(frame_w) * zoom, static_cast<float>(frame_h) * zoom};
  }

  /**
   * @brief Clamp camera scroll so the sheet image remains visible.
   *
   * @param cx, cy      Current camera position.
   * @param zoom        Display scale factor.
   * @param img_w       Image width in pixels.
   * @param img_h       Image height in pixels.
   * @param canvas_w    Canvas width in pixels.
   * @param canvas_h    Canvas height in pixels.
   * @return Clamped {camera_x, camera_y}.
   */
  [[nodiscard]] inline std::pair<float, float> clamp_camera(float cx, float cy, float zoom, int img_w, int img_h,
                                                            int canvas_w, int canvas_h) noexcept {
    const float max_x = std::max(0.f, static_cast<float>(img_w) * zoom - static_cast<float>(canvas_w));
    const float max_y = std::max(0.f, static_cast<float>(img_h) * zoom - static_cast<float>(canvas_h));
    return {std::clamp(cx, 0.f, max_x), std::clamp(cy, 0.f, max_y)};
  }

} // namespace tools::sprite
