// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <algorithm>

namespace corundum::core {

  /** @brief Size of the internal render target, in pixels. */
  struct RenderResolution {
    int width{};

    int height{};
  };

  /** @brief Internal render resolution for a window.
   *
   *  Decouples the resolution the game renders at from the display's physical pixel count: the
   *  render target is the logical window size scaled by @p scale and clamped to the physical
   *  framebuffer, then the compositor up-scales it to the panel. This is the fixed form of the
   *  internal-render-resolution technique (Game Engine Architecture): a smaller equal-aspect target
   *  costs proportionally less fill, which is what keeps a high-DPI or oversized display inside its
   *  frame budget.
   *
   *  @p scale is relative to logical window points, not physical pixels: 1.0 renders one pixel per
   *  point, and the display's content scale (2.0 on a Retina panel) is the scale that renders at
   *  native resolution.
   *
   *  @pre None; degenerate inputs are clamped to at least 1x1.
   */
  [[nodiscard]] constexpr RenderResolution compute_render_resolution(int logical_width, int logical_height,
                                                                     int physical_width, int physical_height,
                                                                     float scale) noexcept {
    const auto axis = [](int logical, int physical, float factor) -> int {
      const int requested = static_cast<int>(static_cast<float>(logical) * factor);
      const int ceiling = physical > 0 ? physical : requested;
      return std::max(1, std::min(ceiling, requested));
    };
    return {
        .width = axis(logical_width, physical_width, scale),
        .height = axis(logical_height, physical_height, scale),
    };
  }

} // namespace corundum::core
