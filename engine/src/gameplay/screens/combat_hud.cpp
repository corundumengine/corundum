// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/gameplay/screens/combat_hud.hpp>

#include <corundum/core/math/vec.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/font_family.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/ui/ui_draw.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <string_view>

namespace corundum::gameplay::screens {

  namespace {

    constexpr float k_panel_width = 260.f;
    constexpr float k_pad = 12.f;
    constexpr float k_bar_height = 6.f;
    constexpr float k_bar_gap = 4.f;
    constexpr float k_row_gap = 10.f;
    constexpr float k_command_point_width = 2.f;

    constexpr core::math::Colour k_bar_back{.r = 60, .g = 60, .b = 60, .a = 255};
    constexpr core::math::Colour k_health_fill{.r = 173, .g = 56, .b = 76, .a = 255};
    constexpr core::math::Colour k_marker_fill{.r = 63, .g = 97, .b = 128, .a = 255};

    float body_line_height(const ui::PanelStyle &style) noexcept {
      return std::max(style.line_spacing, static_cast<float>(style.font_size_body) + 4.f);
    }

    /// A bar's fill fraction, clamped to [0, 1]; 0 when @p whole isn't positive.
    float fraction(int part, int whole) noexcept {
      if (whole <= 0)
        return 0.f;
      return std::clamp(static_cast<float>(part) / static_cast<float>(whole), 0.f, 1.f);
    }

    void draw_rect(platform::Renderer &r, core::math::Vec2 pos, core::math::Vec2 size, core::math::Colour colour) {
      r.draw(platform::DrawRect{.position = pos, .size = size, .colour = colour});
    }

    void draw_text(platform::Renderer &r, const ui::PanelStyle &style, std::string_view text, core::math::Vec2 pos,
                   unsigned char_size, core::math::Colour colour) {
      r.draw(platform::DrawText{
          .font_id = style.family(ui::FontRole::Ui).get(ui::FontStyle::Regular),
          .text = text,
          .position = pos,
          .char_size = char_size,
          .colour = colour,
      });
    }

    /// The timeline bar: where the marker is, and a tick at the command point.
    void draw_timeline_bar(platform::Renderer &r, const ui::PanelStyle &style, const CombatHudRow &row,
                           ui::RowRect bar) {
      draw_rect(r, bar.pos, {.x = bar.width, .y = bar.height}, k_bar_back);
      draw_rect(r, bar.pos, {.x = bar.width * fraction(row.marker_beats, row.windup_end_beats), .y = bar.height},
                row.is_winding_up ? style.selected : k_marker_fill);
      const core::math::Vec2 command_pos{
          .x = bar.pos.x + (bar.width * fraction(row.command_point_beats, row.windup_end_beats)),
          .y = bar.pos.y,
      };
      draw_rect(r, command_pos, {.x = k_command_point_width, .y = bar.height}, style.body);
    }

    /// Draw one combatant's row from the top of its cell and return the y below it.
    float draw_row(platform::Renderer &r, const ui::PanelStyle &style, const CombatHudRow &row, float x, float y) {
      const std::string text = std::format("{}  {} / {}", row.name, row.health.current, row.health.max);
      draw_text(r, style, text, {.x = x, .y = y}, style.font_size_body, row.is_hostile ? style.speaker : style.body);
      y += body_line_height(style);

      const float bar_width = k_panel_width - (2.f * k_pad);
      y += k_bar_gap;
      const ui::RowRect health{.pos = {.x = x, .y = y}, .width = bar_width, .height = k_bar_height};
      draw_rect(r, health.pos, {.x = health.width, .y = health.height}, k_bar_back);
      draw_rect(r, health.pos, {.x = health.width * fraction(row.health.current, row.health.max), .y = health.height},
                k_health_fill);
      y += k_bar_height + k_bar_gap;

      const ui::RowRect timeline{.pos = {.x = x, .y = y}, .width = bar_width, .height = k_bar_height};
      draw_timeline_bar(r, style, row, timeline);
      y += k_bar_height;
      return y;
    }

    void draw_banner(platform::Renderer &r, const ui::PanelStyle &style, core::math::Vec2 viewport,
                     std::string_view banner) {
      if (banner.empty())
        return;
      const std::uint32_t font = style.family(ui::FontRole::Ui).get(ui::FontStyle::Regular);
      const float width = r.measure_text(font, banner, style.font_size_speaker);
      draw_text(r, style, banner, {.x = (viewport.x - width) / 2.f, .y = style.margin}, style.font_size_speaker,
                style.speaker);
    }

  } // namespace

  void combat_hud_render(platform::Renderer &r, const ui::PanelStyle &style, const ui::NinePatchBorder &border,
                         std::span<const CombatHudRow> rows, core::math::Vec2 viewport, std::string_view banner) {
    const float row_height = body_line_height(style) + (2.f * (k_bar_gap + k_bar_height));
    const float rows_height = (row_height * static_cast<float>(rows.size())) +
                              (rows.empty() ? 0.f : (k_row_gap * static_cast<float>(rows.size() - 1)));
    const core::math::Vec2 panel_pos{.x = style.margin, .y = style.margin};
    const core::math::Vec2 panel_size{.x = k_panel_width, .y = (2.f * k_pad) + rows_height};
    ui::panel_chrome(r, style.bg, border, panel_pos, panel_size);

    float y = panel_pos.y + k_pad;
    for (std::size_t i = 0; i < rows.size(); ++i) {
      y = draw_row(r, style, rows[i], panel_pos.x + k_pad, y);
      if (i + 1 < rows.size())
        y += k_row_gap;
    }

    draw_banner(r, style, viewport, banner);
  }

} // namespace corundum::gameplay::screens
