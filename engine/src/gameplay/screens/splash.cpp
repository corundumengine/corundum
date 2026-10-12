// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/gameplay/screens/splash.hpp>

#include <corundum/core/math/vec.hpp>
#include <corundum/platform/renderer.hpp>

#include <algorithm>

namespace corundum::gameplay::screens {

  void splash_render(platform::Renderer &r, core::math::Vec2 viewport, const SplashState &state) {
    r.draw(platform::DrawRect{
        .position = {.x = 0.f, .y = 0.f},
        .size = viewport,
        .colour = core::math::Colour{.r = 0, .g = 0, .b = 0, .a = 255},
    });
    if (state.index >= state.images.size())
      return;
    const SplashImage &image = state.images[state.index];
    if (image.texture == 0 || image.size.x <= 0.f || image.size.y <= 0.f)
      return;

    // Contain, not cover: a logo must never be cropped. Fit within a centered box smaller than the
    // viewport so a tightly cropped logo keeps a black margin.
    const float scale = std::min(viewport.x * k_splash_max_viewport_fraction / image.size.x,
                                 viewport.y * k_splash_max_viewport_fraction / image.size.y);
    const core::math::Vec2 drawn_size{.x = image.size.x * scale, .y = image.size.y * scale};
    r.draw(platform::DrawSprite{
        .texture_id = image.texture,
        .position = {.x = (viewport.x - drawn_size.x) * 0.5f, .y = (viewport.y - drawn_size.y) * 0.5f},
        .source = {.x = 0, .y = 0, .width = static_cast<int>(image.size.x), .height = static_cast<int>(image.size.y)},
        .scale = {.x = scale, .y = scale},
    });
  }

} // namespace corundum::gameplay::screens
