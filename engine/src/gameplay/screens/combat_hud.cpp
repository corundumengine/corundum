// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/gameplay/screens/combat_hud.hpp>

#include <corundum/core/math/vec.hpp>
#include <corundum/input/actions.hpp>
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
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace corundum::gameplay::screens {

  namespace {

    /// Fits "Brann  16 / 16  Crushing Blow" at the default body size, so the panel doesn't change
    /// width with each ordinary wind-up. A longer row still widens the panel.
    constexpr float k_min_row_width = 300.f;
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

    /// A panel's inset: content must clear the nine-patch corners, whose cells draw at natural size.
    core::math::Vec2 panel_padding(const ui::NinePatchBorder &border) noexcept {
      return {
          .x = std::max(k_pad, static_cast<float>(border.tile_w)),
          .y = std::max(k_pad, static_cast<float>(border.tile_h)),
      };
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

    /// One combatant's heading: name and Health, then the command winding up when there is one.
    std::string row_text(const CombatHudRow &row) {
      if (row.windup_label.empty())
        return std::format("{}  {} / {}", row.name, row.health.current, row.health.max);
      return std::format("{}  {} / {}  {}", row.name, row.health.current, row.health.max, row.windup_label);
    }

    /// Join @p parts with ", ".
    std::string joined(std::span<const std::string> parts) {
      std::string text;
      for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i != 0)
          text += ", ";
        text += parts[i];
      }
      return text;
    }

    /// One body line of a row: its name, then its status labels when it has any.
    float row_heading_height(const ui::PanelStyle &style, const CombatHudRow &row) noexcept {
      return row.status_labels.empty() ? body_line_height(style) : 2.f * body_line_height(style);
    }

    /// A row's name colour: the selected colour for the fight's target, else the hostile's own.
    core::math::Colour row_colour(const ui::PanelStyle &style, const CombatHudRow &row) noexcept {
      if (row.is_target)
        return style.selected;
      return row.is_hostile ? style.speaker : style.body;
    }

    /// Draw one combatant's row from the top of its cell and return the y below it.
    float draw_row(platform::Renderer &r, const ui::PanelStyle &style, const CombatHudRow &row, std::string_view text,
                   std::string_view statuses, core::math::Vec2 pos, float bar_width) {
      const float x = pos.x;
      float y = pos.y;
      draw_text(r, style, text, pos, style.font_size_body, row_colour(style, row));
      y += body_line_height(style);

      if (!statuses.empty()) {
        draw_text(r, style, statuses, {.x = x, .y = y}, style.font_size_body, style.body);
        y += body_line_height(style);
      }

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

    /// The panel at the top-left: one row per combatant, as wide as its longest row needs.
    void draw_rows_panel(platform::Renderer &r, const ui::PanelStyle &style, const ui::NinePatchBorder &border,
                         std::span<const CombatHudRow> rows) {
      const std::uint32_t font = style.family(ui::FontRole::Ui).get(ui::FontStyle::Regular);
      std::vector<std::string> texts;
      std::vector<std::string> statuses;
      texts.reserve(rows.size());
      statuses.reserve(rows.size());
      float rows_width = k_min_row_width;
      float rows_height = 0.f;
      for (const CombatHudRow &row : rows) {
        texts.push_back(row_text(row));
        statuses.push_back(joined(row.status_labels));
        rows_width = std::max(rows_width, r.measure_text(font, texts.back(), style.font_size_body));
        if (!statuses.back().empty())
          rows_width = std::max(rows_width, r.measure_text(font, statuses.back(), style.font_size_body));
        rows_height += row_heading_height(style, row) + (2.f * (k_bar_gap + k_bar_height));
      }
      if (!rows.empty())
        rows_height += k_row_gap * static_cast<float>(rows.size() - 1);

      const core::math::Vec2 pad = panel_padding(border);
      const core::math::Vec2 panel_pos{.x = style.margin, .y = style.margin};
      const core::math::Vec2 panel_size{.x = rows_width + (2.f * pad.x), .y = rows_height + (2.f * pad.y)};
      ui::panel_chrome(r, style.bg, border, panel_pos, panel_size);

      float y = panel_pos.y + pad.y;
      for (std::size_t i = 0; i < rows.size(); ++i) {
        y = draw_row(r, style, rows[i], texts[i], statuses[i], {.x = panel_pos.x + pad.x, .y = y}, rows_width);
        if (i + 1 < rows.size())
          y += k_row_gap;
      }
    }

    /// One item on a command-bar line: its text, and the colour it draws in.
    struct BarItem {
      std::string text{};

      core::math::Colour colour{};
    };

    /// The width of @p items' texts and the gaps between them.
    float items_width(const platform::Renderer &r, std::uint32_t font, const ui::PanelStyle &style,
                      std::span<const BarItem> items) {
      float width = 0.f;
      for (const BarItem &item : items)
        width += r.measure_text(font, item.text, style.font_size_body);
      if (!items.empty())
        width += k_command_gap * static_cast<float>(items.size() - 1);
      return width;
    }

    /// Draw @p items in a row from @p left.
    void draw_items(platform::Renderer &r, const ui::PanelStyle &style, std::uint32_t font,
                    std::span<const BarItem> items, float left, float y) {
      float x = left;
      for (const BarItem &item : items) {
        draw_text(r, style, item.text, {.x = x, .y = y}, style.font_size_body, item.colour);
        x += r.measure_text(font, item.text, style.font_size_body) + k_command_gap;
      }
    }

    /// One command-bar slot's text: the device's glyph, then the command's label. A gamepad draws a
    /// slot off the shown page with its label only.
    std::string command_text(const CombatHudCommand &command, input::InputDevice device) {
      if (device != input::InputDevice::Gamepad)
        return std::format("{} {}", ui::input_glyph(command.action, device), command.label);
      if (!command.gamepad_action)
        return command.label;
      return std::format("{} {}", ui::input_glyph(*command.gamepad_action, device), command.label);
    }

    /// A slot's colour: highlighted when queued or armed, dimmed when unavailable, else the body colour.
    core::math::Colour command_colour(const ui::PanelStyle &style, const CombatHudCommand &command) noexcept {
      if (command.is_highlighted)
        return style.selected;
      return command.is_available ? style.body : k_dimmed;
    }

    /// The command bar along the bottom: the commands' line over a panel sized around it, the page
    /// hint on a gamepad, and the toggles' line above it.
    void draw_command_bar(platform::Renderer &r, const ui::PanelStyle &style, const ui::NinePatchBorder &border,
                          const CombatHud &hud, core::math::Vec2 viewport, input::InputDevice device) {
      if (hud.commands.empty())
        return;

      const std::uint32_t font = style.family(ui::FontRole::Ui).get(ui::FontStyle::Regular);
      std::vector<BarItem> commands;
      commands.reserve(hud.commands.size() + 1);
      for (const CombatHudCommand &command : hud.commands)
        commands.push_back({.text = command_text(command, device), .colour = command_colour(style, command)});
      if (device == input::InputDevice::Gamepad && hud.page.count > 1) {
        const std::string page_hint =
            std::format("{}/{} {}/{}", ui::input_glyph(input::Action::CombatPagePrev, device),
                        ui::input_glyph(input::Action::CombatPageNext, device), hud.page.index + 1, hud.page.count);
        commands.push_back({.text = page_hint, .colour = style.body});
      }

      std::vector<BarItem> toggles;
      toggles.reserve(hud.toggles.size());
      for (const CombatHudCommand &toggle : hud.toggles)
        toggles.push_back({.text = command_text(toggle, device), .colour = command_colour(style, toggle)});

      const float commands_width = items_width(r, font, style, commands);
      const float toggles_width = items_width(r, font, style, toggles);
      const float line_height = body_line_height(style);
      const float toggles_height = toggles.empty() ? 0.f : line_height;
      const float texts_width = std::max(commands_width, toggles_width);

      const core::math::Vec2 pad = panel_padding(border);
      const float commands_top = viewport.y - style.margin - pad.y - line_height;
      const core::math::Vec2 panel_pos{
          .x = ((viewport.x - texts_width) / 2.f) - pad.x,
          .y = commands_top - toggles_height - pad.y,
      };
      const core::math::Vec2 panel_size{
          .x = texts_width + (2.f * pad.x),
          .y = line_height + toggles_height + (2.f * pad.y),
      };
      ui::panel_chrome(r, style.bg, border, panel_pos, panel_size);

      draw_items(r, style, font, commands, (viewport.x - commands_width) / 2.f, commands_top);
      if (!toggles.empty())
        draw_items(r, style, font, toggles, (viewport.x - toggles_width) / 2.f, commands_top - line_height);
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
    draw_rows_panel(r, style, border, hud.rows);
    draw_command_bar(r, style, border, hud, viewport, device);
    draw_banner(r, style, viewport, banner);
  }

} // namespace corundum::gameplay::screens
