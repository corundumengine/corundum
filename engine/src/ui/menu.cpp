// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/ui/menu.hpp>

#include <corundum/core/math/vec.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/input_glyph.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/ui/ui_draw.hpp>

#include <algorithm>
#include <format>
#include <string>
#include <string_view>

namespace corundum::ui {

  std::string_view menu_command_label(MenuCommand command) noexcept {
    switch (command) {
      case MenuCommand::Resume:
        return "Resume";
      case MenuCommand::Settings:
        return "Settings";
      case MenuCommand::Quit:
        return "Quit";
    }
    return "";
  }

  MenuCommand menu_command_at(int row) noexcept {
    switch (row) {
      case 0:
        return MenuCommand::Resume;
      case 1:
        return MenuCommand::Settings;
      case 2:
        return MenuCommand::Quit;
      default:
        return MenuCommand::Resume;
    }
  }

  void menu_panel_render(platform::Renderer &r, const PanelStyle &style, const NinePatchBorder &border,
                         const MenuState &state, core::math::Vec2 viewport, input::InputDevice last_device) {
    constexpr float k_min_w = 220.f;
    constexpr float k_pad_x = 28.f;
    constexpr float k_pad_y = 18.f;
    constexpr float k_title_gap = 12.f;
    constexpr float k_footer_gap = 14.f;
    constexpr std::string_view k_title = "Paused";

    const float line_h = std::max(style.line_spacing, static_cast<float>(style.font_size_body) + 6.f);
    const float title_h = std::max(line_h, static_cast<float>(style.font_size_speaker) + 6.f);
    const float cursor_w = cursor_advance(r, style);

    const std::string footer = std::format("{} Select   {} Close", input_glyph(input::Action::Activate, last_device),
                                           input_glyph(input::Action::Cancel, last_device));

    float widest = std::max(r.measure_text(style.font_id, k_title, style.font_size_speaker),
                            r.measure_text(style.font_id, footer, style.font_size_body));
    for (int row = 0; row < k_menu_command_count; ++row) {
      widest = std::max(widest, cursor_w + r.measure_text(style.font_id, menu_command_label(menu_command_at(row)),
                                                          style.font_size_body));
    }

    const float panel_w = std::max(k_min_w, widest + (k_pad_x * 2.f));
    const float panel_h = (k_pad_y * 2.f) + title_h + k_title_gap +
                          (static_cast<float>(k_menu_command_count) * line_h) + k_footer_gap + line_h;
    const float panel_x = (viewport.x - panel_w) * 0.5f;
    const float panel_y = (viewport.y - panel_h) * 0.5f;

    panel_chrome(r, style.bg, border, {.x = panel_x, .y = panel_y}, {.x = panel_w, .y = panel_h});

    const float title_w = r.measure_text(style.font_id, k_title, style.font_size_speaker);
    const float title_y = panel_y + k_pad_y;
    r.draw(platform::DrawText{
        .font_id = style.font_id,
        .text = k_title,
        .position = {.x = panel_x + ((panel_w - title_w) * 0.5f), .y = title_y},
        .char_size = style.font_size_speaker,
        .colour = style.speaker,
    });

    r.draw(platform::DrawText{
        .font_id = style.font_id,
        .text = footer,
        .position =
            {
                .x = panel_x + ((panel_w - r.measure_text(style.font_id, footer, style.font_size_body)) * 0.5f),
                .y = panel_y + panel_h - k_pad_y - line_h,
            },
        .char_size = style.font_size_body,
        .colour = style.choice,
    });

    const int clamped_cursor = std::clamp(state.cursor, 0, k_menu_command_count - 1);
    float y = title_y + title_h + k_title_gap;
    for (int row = 0; row < k_menu_command_count; ++row) {
      draw_option(r, style, menu_command_label(menu_command_at(row)), {.x = panel_x + k_pad_x, .y = y},
                  row == clamped_cursor);
      y += line_h;
    }
  }

} // namespace corundum::ui
