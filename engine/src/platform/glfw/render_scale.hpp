// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <cmath>
#include <cstdint>

namespace corundum::platform::glfw {

  /// Per-axis logical-point -> physical-pixel factors for screen-space rendering.
  struct ScreenScale {
    float x{1.f};
    float y{1.f};
  };

  /** @brief Derive the content scale from a framebuffer/window size pair.
   *
   * Deriving the scale from the same measured pair the screen projection spans keeps emitted
   * geometry and the projection from disagreeing — unlike querying a second API
   * (glfwGetWindowContentScale) that can round or lag independently.
   *
   * @pre All sizes are >= 0.
   * @return Per-axis scale; 1.0 on an axis whose logical size is zero (degenerate window).
   */
  [[nodiscard]] constexpr ScreenScale derive_screen_scale(int fb_w, int fb_h, int win_w, int win_h) noexcept {
    const float sx = win_w > 0 ? static_cast<float>(fb_w) / static_cast<float>(win_w) : 1.f;
    const float sy = win_h > 0 ? static_cast<float>(fb_h) / static_cast<float>(win_h) : 1.f;
    return {.x = sx, .y = sy};
  }

  /** @brief Rasterisation size in physical pixels for a logical font size.
   *
   * FreeType's pixel size is a height, so the vertical scale is the one that applies.
   * @pre scale_y > 0.
   */
  [[nodiscard]] inline uint32_t physical_font_size(uint32_t char_size, float scale_y) noexcept {
    return static_cast<uint32_t>(std::lround(static_cast<float>(char_size) * scale_y));
  }

  /** @brief Convert a physical-pixel metric back to logical units. @pre scale > 0. */
  [[nodiscard]] constexpr float to_logical(float physical, float scale) noexcept {
    return physical / scale;
  }

} // namespace corundum::platform::glfw
