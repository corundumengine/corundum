// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/math/vec.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/screen_transition.hpp>

#include <algorithm>
#include <cstdint>

namespace corundum::ui {

  void render_screen_transition(platform::Renderer &r, const ScreenTransition &transition, core::math::Vec2 viewport) {
    const float alpha = std::clamp(transition.alpha(), 0.f, 1.f);
    if (alpha <= 0.f)
      return;
    const auto channel = static_cast<std::uint8_t>(alpha * 255.f);
    r.draw(platform::DrawRect{
        .position = {.x = 0.f, .y = 0.f},
        .size = viewport,
        .colour = core::math::Colour{.r = 0, .g = 0, .b = 0, .a = channel},
    });
  }

} // namespace corundum::ui
