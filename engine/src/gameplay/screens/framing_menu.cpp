// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include "gameplay/screens/framing_menu.hpp"

#include <corundum/core/math/vec.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/font_family.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/ui/ui_draw.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

namespace corundum::gameplay::screens::detail {

  namespace {
    constexpr float k_framing_min_w = 320.f;
    constexpr float k_framing_pad_x = 36.f;
    constexpr float k_framing_pad_y = 24.f;
    constexpr float k_framing_heading_gap = 20.f;
    constexpr float k_framing_footer_gap = 16.f;
  } // namespace

  FramingMenuLayout framing_menu_layout(const platform::Renderer &r, const ui::PanelStyle &style,
                                        core::math::Vec2 viewport, std::span<const std::string_view> labels,
                                        std::string_view heading, std::string_view footer) {
    const float line_h = std::max(style.line_spacing, static_cast<float>(style.font_size_body) + 6.f);
    const float heading_h = std::max(line_h, static_cast<float>(style.font_size_heading) + 8.f);
    const float cursor_w = ui::cursor_advance(r, style);
    const std::uint32_t body_font = style.family(ui::FontRole::Ui).get(ui::FontStyle::Regular);
    const std::uint32_t heading_font = style.family(ui::FontRole::Display).get(ui::FontStyle::Regular);

    float widest = std::max(r.measure_text(heading_font, heading, style.font_size_heading),
                            r.measure_text(body_font, footer, style.font_size_body));
    for (const std::string_view label : labels)
      widest = std::max(widest, cursor_w + r.measure_text(body_font, label, style.font_size_body));

    const float panel_w = std::max(k_framing_min_w, widest + (k_framing_pad_x * 2.f));
    const float panel_h = (k_framing_pad_y * 2.f) + heading_h + k_framing_heading_gap +
                          (static_cast<float>(labels.size()) * line_h) + k_framing_footer_gap + line_h;
    const float panel_x = (viewport.x - panel_w) * 0.5f;
    const float panel_y = (viewport.y - panel_h) * 0.5f;

    FramingMenuLayout layout{};
    layout.panel_pos = {.x = panel_x, .y = panel_y};
    layout.panel_size = {.x = panel_w, .y = panel_h};
    layout.rows = ui::ListHit{
        .row_pos = {.x = panel_x + k_framing_pad_x, .y = panel_y + k_framing_pad_y + heading_h + k_framing_heading_gap},
        .row_width = panel_w - (k_framing_pad_x * 2.f),
        .row_height = line_h,
        .first_row = 0,
        .visible_rows = static_cast<int>(labels.size()),
    };
    return layout;
  }

  void framing_menu_render(platform::Renderer &r, const ui::PanelStyle &style, const ui::NinePatchBorder &border,
                           const FramingMenuLayout &layout, std::span<const std::string_view> labels,
                           std::span<const bool> enabled, int cursor, std::string_view heading, std::string_view footer,
                           core::math::Vec2 viewport) {
    const float line_h = layout.rows.row_height;
    const std::uint32_t body_font = style.family(ui::FontRole::Ui).get(ui::FontStyle::Regular);
    const std::uint32_t heading_font = style.family(ui::FontRole::Display).get(ui::FontStyle::Regular);

    // The framing screen sits over the scene the engine loaded at startup; an opaque backdrop
    // keeps that world from showing through.
    ui::screen_backdrop(r, style, viewport);
    ui::panel_chrome(r, style.bg, border, layout.panel_pos, layout.panel_size);

    const float heading_w = r.measure_text(heading_font, heading, style.font_size_heading);
    r.draw(platform::DrawText{
        .font_id = heading_font,
        .text = heading,
        .position =
            {
                .x = layout.panel_pos.x + ((layout.panel_size.x - heading_w) * 0.5f),
                .y = layout.panel_pos.y + k_framing_pad_y,
            },
        .char_size = style.font_size_heading,
        .colour = style.speaker,
    });

    r.draw(platform::DrawText{
        .font_id = body_font,
        .text = footer,
        .position =
            {
                .x = layout.panel_pos.x +
                     ((layout.panel_size.x - r.measure_text(body_font, footer, style.font_size_body)) * 0.5f),
                .y = layout.panel_pos.y + layout.panel_size.y - k_framing_pad_y - line_h,
            },
        .char_size = style.font_size_body,
        .colour = style.choice,
    });

    const int row_count = static_cast<int>(labels.size());
    const int clamped_cursor = std::clamp(cursor, 0, row_count > 0 ? row_count - 1 : 0);
    float y = layout.rows.row_pos.y;
    for (int row = 0; row < row_count; ++row) {
      const bool row_enabled = std::cmp_less(row, enabled.size()) ? enabled[static_cast<std::size_t>(row)] : true;
      const bool selected = row_enabled && row == clamped_cursor;
      ui::draw_option(r, style, labels[static_cast<std::size_t>(row)], {.x = layout.rows.row_pos.x, .y = y}, selected);
      y += line_h;
    }
  }

} // namespace corundum::gameplay::screens::detail
