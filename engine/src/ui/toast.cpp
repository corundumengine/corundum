// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/ui/toast.hpp>

#include <corundum/core/math/vec.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/dialog_box.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/ui/ui_draw.hpp>

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace corundum::ui {

  void ToastQueue::notify(std::string text, core::math::Colour colour) {
    toasts_.push_back(Toast{.text = std::move(text), .colour = colour, .remaining = k_toast_ttl_seconds});
    if (toasts_.size() > k_toast_max_visible)
      toasts_.erase(toasts_.begin(),
                    toasts_.begin() + static_cast<std::ptrdiff_t>(toasts_.size() - k_toast_max_visible));
  }

  void ToastQueue::update(float dt) noexcept {
    for (Toast &toast : toasts_)
      toast.remaining -= dt;
    std::erase_if(toasts_, [](const Toast &toast) { return toast.remaining <= 0.f; });
  }

  void ToastQueue::render(platform::Renderer &r, const DialogBoxStyle &style, core::math::Vec2 viewport) const {
    if (toasts_.empty())
      return;

    constexpr float k_pad_x = 12.f;
    constexpr float k_pad_y = 8.f;
    constexpr float k_margin = 16.f;
    constexpr float k_gap = 4.f;

    const float line_h = std::max(style.line_spacing, static_cast<float>(style.font_size_body) + 4.f);
    const float row_h = line_h + k_pad_y;
    const float total_h =
        (static_cast<float>(toasts_.size()) * row_h) + (static_cast<float>(toasts_.size() - 1) * k_gap);
    const NinePatchBorder no_border{};

    // Oldest first, then downward, so the newest toast sits nearest the bottom edge.
    float y = viewport.y - k_margin - total_h;
    for (const Toast &toast : toasts_) {
      const float text_w = r.measure_text(style.font_id, toast.text, style.font_size_body);
      const float row_w = text_w + (k_pad_x * 2.f);
      panel_chrome(r, style.bg, no_border, {.x = k_margin, .y = y}, {.x = row_w, .y = row_h});
      r.draw(platform::DrawText{
          .font_id = style.font_id,
          .text = toast.text,
          .position = {.x = k_margin + k_pad_x, .y = y + (k_pad_y * 0.5f)},
          .char_size = style.font_size_body,
          .colour = toast.colour,
      });
      y += row_h + k_gap;
    }
  }

} // namespace corundum::ui
