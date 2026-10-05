// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/ui/font_family.hpp>
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
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace corundum::ui {

  namespace {

    constexpr float k_menu_min_w = 220.f;
    constexpr float k_menu_pad_x = 28.f;
    constexpr float k_menu_pad_y = 18.f;
    constexpr float k_menu_title_gap = 12.f;
    constexpr float k_menu_footer_gap = 14.f;
    constexpr std::string_view k_menu_title = "Paused";

    /// Footer hint, using the last-used device's glyphs.
    std::string menu_footer(input::InputDevice last_device) {
      return std::format("{} Select   {} Close", input_glyph(input::Action::Activate, last_device),
                         input_glyph(input::Action::Cancel, last_device));
    }

  } // namespace

  std::string_view menu_command_label(MenuCommand command) noexcept {
    switch (command) {
      case MenuCommand::Resume:
        return "Resume";
      case MenuCommand::Settings:
        return "Settings";
      case MenuCommand::Save:
        return "Save";
      case MenuCommand::Load:
        return "Load";
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
        return MenuCommand::Save;
      case 3:
        return MenuCommand::Load;
      case 4:
        return MenuCommand::Quit;
      default:
        return MenuCommand::Resume;
    }
  }

  MenuLayout menu_panel_layout(const platform::Renderer &r, const PanelStyle &style, core::math::Vec2 viewport,
                               input::InputDevice last_device) {
    const float line_h = std::max(style.line_spacing, static_cast<float>(style.font_size_body) + 6.f);
    const float title_h = std::max(line_h, static_cast<float>(style.font_size_speaker) + 6.f);
    const float cursor_w = cursor_advance(r, style);

    const std::string footer = menu_footer(last_device);
    const std::uint32_t font_id = style.family(FontRole::Ui).get(FontStyle::Regular);

    float widest = std::max(r.measure_text(font_id, k_menu_title, style.font_size_speaker),
                            r.measure_text(font_id, footer, style.font_size_body));
    for (int row = 0; row < k_menu_command_count; ++row) {
      widest = std::max(
          widest, cursor_w + r.measure_text(font_id, menu_command_label(menu_command_at(row)), style.font_size_body));
    }

    const float panel_w = std::max(k_menu_min_w, widest + (k_menu_pad_x * 2.f));
    const float panel_h = (k_menu_pad_y * 2.f) + title_h + k_menu_title_gap +
                          (static_cast<float>(k_menu_command_count) * line_h) + k_menu_footer_gap + line_h;
    const float panel_x = (viewport.x - panel_w) * 0.5f;
    const float panel_y = (viewport.y - panel_h) * 0.5f;

    MenuLayout layout{};
    layout.panel_pos = {.x = panel_x, .y = panel_y};
    layout.panel_size = {.x = panel_w, .y = panel_h};
    layout.rows = ui::ListHit{
        .row_pos = {.x = panel_x + k_menu_pad_x, .y = panel_y + k_menu_pad_y + title_h + k_menu_title_gap},
        .row_width = panel_w - (k_menu_pad_x * 2.f),
        .row_height = line_h,
        .first_row = 0,
        .visible_rows = k_menu_command_count,
    };
    return layout;
  }

  void menu_panel_render(platform::Renderer &r, const PanelStyle &style, const NinePatchBorder &border,
                         const MenuState &state, core::math::Vec2 viewport, input::InputDevice last_device) {
    const MenuLayout layout = menu_panel_layout(r, style, viewport, last_device);
    const float line_h = layout.rows.row_height;

    const std::string footer = menu_footer(last_device);
    const std::uint32_t font_id = style.family(FontRole::Ui).get(FontStyle::Regular);

    panel_chrome(r, style.bg, border, layout.panel_pos, layout.panel_size);

    const float title_w = r.measure_text(font_id, k_menu_title, style.font_size_speaker);
    const float title_y = layout.panel_pos.y + k_menu_pad_y;
    r.draw(platform::DrawText{
        .font_id = font_id,
        .text = k_menu_title,
        .position = {.x = layout.panel_pos.x + ((layout.panel_size.x - title_w) * 0.5f), .y = title_y},
        .char_size = style.font_size_speaker,
        .colour = style.speaker,
    });

    r.draw(platform::DrawText{
        .font_id = font_id,
        .text = footer,
        .position =
            {
                .x = layout.panel_pos.x +
                     ((layout.panel_size.x - r.measure_text(font_id, footer, style.font_size_body)) * 0.5f),
                .y = layout.panel_pos.y + layout.panel_size.y - k_menu_pad_y - line_h,
            },
        .char_size = style.font_size_body,
        .colour = style.choice,
    });

    const int clamped_cursor = std::clamp(state.cursor, 0, k_menu_command_count - 1);
    float y = layout.rows.row_pos.y;
    for (int row = 0; row < k_menu_command_count; ++row) {
      draw_option(r, style, menu_command_label(menu_command_at(row)), {.x = layout.rows.row_pos.x, .y = y},
                  row == clamped_cursor);
      y += line_h;
    }
  }

} // namespace corundum::ui
