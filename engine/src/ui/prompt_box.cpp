// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/math/vec.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/dialog_box.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/ui/prompt_box.hpp>

#include <corundum/ui/ui_draw.hpp>

#include <algorithm>
#include <string_view>

namespace corundum::ui {

  void prompt_box_render(platform::Renderer &r, const DialogBoxStyle &style, const NinePatchBorder &border,
                         std::string_view question, bool yes_selected, core::math::Vec2 viewport) {
    constexpr float k_min_w = 200.f;
    constexpr float k_pad_x = 32.f;
    constexpr float k_pad_y = 24.f;
    constexpr float k_gap = 22.f;
    constexpr float k_opt_gap = 40.f;
    constexpr std::string_view k_yes = "Yes";
    constexpr std::string_view k_no = "No";

    const float q_w = r.measure_text(style.font_id, question, style.font_size_body);
    const float yes_w = r.measure_text(style.font_id, k_yes, style.font_size_body);
    const float no_w = r.measure_text(style.font_id, k_no, style.font_size_body);
    // draw_option always advances its label past the cursor column, selected or not, so
    // each column is cursor + label.
    const float cursor_w = cursor_advance(r, style);
    const float yes_col_w = cursor_w + yes_w;
    const float no_col_w = cursor_w + no_w;
    const float options_row_w = yes_col_w + k_opt_gap + no_col_w;

    const float content_w = std::max(q_w, options_row_w);
    const float panel_w = std::max(k_min_w, content_w + (k_pad_x * 2.f));
    const float line_h = std::max(style.line_spacing, static_cast<float>(style.font_size_body) + 4.f);
    const float panel_h = (k_pad_y * 2.f) + line_h + k_gap + line_h;

    const float panel_x = (viewport.x - panel_w) * 0.5f;
    const float panel_y = (viewport.y - panel_h) * 0.5f;

    panel_chrome(r, style.bg, border, {.x = panel_x, .y = panel_y}, {.x = panel_w, .y = panel_h});

    const float q_x = panel_x + ((panel_w - q_w) * 0.5f);
    const float q_y = panel_y + k_pad_y;
    r.draw(platform::DrawText{
        .font_id = style.font_id,
        .text = question,
        .position = {.x = q_x, .y = q_y},
        .char_size = style.font_size_body,
        .colour = style.body,
    });

    const float opt_y = q_y + line_h + k_gap;
    const float row_x = panel_x + ((panel_w - options_row_w) * 0.5f);
    draw_option(r, style, k_yes, {.x = row_x, .y = opt_y}, yes_selected);
    draw_option(r, style, k_no, {.x = row_x + yes_col_w + k_opt_gap, .y = opt_y}, !yes_selected);
  }

} // namespace corundum::ui
