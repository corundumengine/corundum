#pragma once

/// @brief Tilesmith window and panel layout constants.

namespace tools::tilemap {

  constexpr int k_menu_h = 26; ///< Height reserved for the top menu bar (matches STATUS_H).
  constexpr int WINDOW_W = 1900;
  constexpr int WINDOW_H = 1000 + k_menu_h;
  constexpr int PALETTE_W = 480;
  constexpr int STATUS_H = 26;
  constexpr int CANVAS_W = WINDOW_W - PALETTE_W;           ///< 1420
  constexpr int CANVAS_H = WINDOW_H - STATUS_H - k_menu_h; ///< 974, unchanged from before this plan
  constexpr int LAYER_ROW_H = 28;
  constexpr int LAYER_TITLE_H = 30; ///< Height of the "Layers" header row above the layer list.

  constexpr float LAYER_BTN_W = 22.f;                 ///< Width of add/delete layer buttons.
  constexpr float LAYER_BTN_H = 22.f;                 ///< Height of add/delete layer buttons.
  constexpr float LAYER_BTN_ADD_X = PALETTE_W - 26.f; ///< Left edge of the "+" (add layer) button.
  constexpr float LAYER_BTN_DEL_X = PALETTE_W - 48.f; ///< Left edge of the "−" (delete layer) button.

} // namespace tools::tilemap
