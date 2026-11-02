// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/gameplay/screens/combat_hud.hpp>

#include <corundum/core/math/vec.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/font_family.hpp>
#include <corundum/ui/input_glyph.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/ui/ui_draw.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace corundum::gameplay::screens {

  namespace {

    constexpr float k_panel_width = 260.f;
    constexpr float k_pad = 12.f;
    constexpr float k_bar_height = 6.f;
    constexpr float k_bar_gap = 4.f;
    constexpr float k_row_gap = 10.f;
    constexpr float k_command_gap = 28.f;
    constexpr float k_command_point_width = 2.f;

    constexpr core::math::Colour k_bar_back{.r = 60, .g = 60, .b = 60, .a = 255};
    constexpr core::math::Colour k_dimmed{.r = 110, .g = 110, .b = 110, .a = 255};
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
      const std::string text =
          row.windup_label.empty()
              ? std::format("{}  {} / {}", row.name, row.health.current, row.health.max)
              : std::format("{}  {} / {}  {}", row.name, row.health.current, row.health.max, row.windup_label);
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

    /// One command-bar slot's text: the device's glyph, then the command's label.
    std::string command_text(const CombatHudCommand &command, input::InputDevice device) {
      return std::format("{} {}", ui::input_glyph(command.action, device), command.label);
    }

    /// A slot's colour: highlighted when queued, dimmed when unavailable, else the body colour.
    core::math::Colour command_colour(const ui::PanelStyle &style, const CombatHudCommand &command) noexcept {
      if (command.is_queued)
        return style.selected;
      return command.is_available ? style.body : k_dimmed;
    }

    /// The command bar along the bottom: the slots' texts over a panel sized around them.
    void draw_command_bar(platform::Renderer &r, const ui::PanelStyle &style, const ui::NinePatchBorder &border,
                          const CombatHud &hud, core::math::Vec2 viewport, input::InputDevice device) {
      if (hud.commands.empty())
        return;

      const std::uint32_t font = style.family(ui::FontRole::Ui).get(ui::FontStyle::Regular);
      std::vector<std::string> texts;
      texts.reserve(hud.commands.size());
      float texts_width = 0.f;
      for (const CombatHudCommand &command : hud.commands) {
        texts.push_back(command_text(command, device));
        texts_width += r.measure_text(font, texts.back(), style.font_size_body);
      }
      texts_width += k_command_gap * static_cast<float>(texts.size() - 1);

      const float line_height = body_line_height(style);
      const float left = (viewport.x - texts_width) / 2.f;
      const float top = viewport.y - style.margin - line_height;
      const core::math::Vec2 panel_pos{.x = left - k_pad, .y = top - k_pad};
      const core::math::Vec2 panel_size{.x = texts_width + (2.f * k_pad), .y = line_height + (2.f * k_pad)};
      ui::panel_chrome(r, style.bg, border, panel_pos, panel_size);

      float x = left;
      for (std::size_t i = 0; i < texts.size(); ++i) {
        draw_text(r, style, texts[i], {.x = x, .y = top}, style.font_size_body, command_colour(style, hud.commands[i]));
        x += r.measure_text(font, texts[i], style.font_size_body) + k_command_gap;
      }
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
                         const CombatHud &hud, core::math::Vec2 viewport, input::InputDevice device,
                         std::string_view banner) {
    const float row_height = body_line_height(style) + (2.f * (k_bar_gap + k_bar_height));
    const float rows_height = (row_height * static_cast<float>(hud.rows.size())) +
                              (hud.rows.empty() ? 0.f : (k_row_gap * static_cast<float>(hud.rows.size() - 1)));
    const core::math::Vec2 panel_pos{.x = style.margin, .y = style.margin};
    const core::math::Vec2 panel_size{.x = k_panel_width, .y = (2.f * k_pad) + rows_height};
    ui::panel_chrome(r, style.bg, border, panel_pos, panel_size);

    float y = panel_pos.y + k_pad;
    for (std::size_t i = 0; i < hud.rows.size(); ++i) {
      y = draw_row(r, style, hud.rows[i], panel_pos.x + k_pad, y);
      if (i + 1 < hud.rows.size())
        y += k_row_gap;
    }

    draw_command_bar(r, style, border, hud, viewport, device);
    draw_banner(r, style, viewport, banner);
  }

} // namespace corundum::gameplay::screens
