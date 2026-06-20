#pragma once

/// @brief Spritesmith window and panel layout constants.

namespace tools::sprite {

  constexpr int WINDOW_W = 1770;
  constexpr int WINDOW_H = 1000;
  constexpr int PANEL_W = 400;
  constexpr int STATUS_H = 26;
  constexpr int PREVIEW_H = 240;
  constexpr int CANVAS_W = WINDOW_W - PANEL_W;  ///< 1370
  constexpr int CANVAS_H = WINDOW_H - STATUS_H; ///< 974
  constexpr int PANEL_H = CANVAS_H - PREVIEW_H; ///< 734

} // namespace tools::sprite
