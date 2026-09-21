// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/math/vec.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/nine_patch.hpp>

#include <algorithm>

namespace corundum::ui {

  void nine_patch_render(platform::Renderer &r, const NinePatchBorder &border, float x, float y, float w, float h) {
    if (border.texture_id == 0 || border.tile_w <= 0 || border.tile_h <= 0)
      return;

    const float corner_w = static_cast<float>(border.tile_w);
    const float corner_h = static_cast<float>(border.tile_h);

    const auto source = [&](int col, int row) -> core::math::IntRect {
      return {.x = col * border.tile_w, .y = row * border.tile_h, .width = border.tile_w, .height = border.tile_h};
    };

    // DrawSprite::scale multiplies the source cell, and position is the quad's top-left,
    // so the edge scales below stretch a cell to the inner span.
    const auto emit = [&](core::math::IntRect src, float px, float py, float scale_x = 1.f, float scale_y = 1.f) {
      r.draw(platform::DrawSprite{
          .texture_id = border.texture_id,
          .position = {.x = px, .y = py},
          .source = src,
          .scale = {.x = scale_x, .y = scale_y},
      });
    };

    // Corners — drawn at natural cell size.
    emit(source(0, 0), x, y);
    emit(source(2, 0), x + w - corner_w, y);
    emit(source(0, 2), x, y + h - corner_h);
    emit(source(2, 2), x + w - corner_w, y + h - corner_h);

    const float inner_w = std::max(0.f, w - (corner_w * 2.f));
    const float inner_h = std::max(0.f, h - (corner_h * 2.f));

    // Horizontal edges — stretch X to fill the inner width.
    emit(source(1, 0), x + corner_w, y, inner_w / corner_w, 1.f);
    emit(source(1, 2), x + corner_w, y + h - corner_h, inner_w / corner_w, 1.f);

    // Vertical edges — stretch Y to fill the inner height.
    emit(source(0, 1), x, y + corner_h, 1.f, inner_h / corner_h);
    emit(source(2, 1), x + w - corner_w, y + corner_h, 1.f, inner_h / corner_h);
  }

} // namespace corundum::ui
