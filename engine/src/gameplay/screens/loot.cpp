// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/gameplay/screens/loot.hpp>

#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/screens/inventory_panel.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/input_glyph.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/ui/ui_draw.hpp>

#include <algorithm>
#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace corundum::gameplay::screens {

  namespace {

    std::vector<std::string> row_labels(const std::vector<InventoryLine> &lines) {
      std::vector<std::string> labels;
      labels.reserve(lines.size());
      for (const InventoryLine &line : lines)
        labels.push_back(std::format("{}  x{}", line.name, line.count));
      return labels;
    }

    /// Draw one pane's rows; the active pane uses ui::draw_option (cursor + selected colour), the
    /// inactive one plain text.
    void draw_pane(platform::Renderer &r, const ui::PanelStyle &style, const std::vector<std::string> &labels, float x,
                   float y, bool active, int cursor, float line_h) {
      constexpr std::string_view k_empty = "(empty)";
      if (labels.empty()) {
        r.draw(platform::DrawText{
            .font_id = style.font_id,
            .text = k_empty,
            .position = {.x = x, .y = y},
            .char_size = style.font_size_body,
            .colour = style.choice,
        });
        return;
      }
      for (std::size_t i = 0; i < labels.size(); ++i) {
        if (active) {
          ui::draw_option(r, style, labels[i], {.x = x, .y = y}, std::cmp_equal(i, cursor));
        } else {
          r.draw(platform::DrawText{
              .font_id = style.font_id,
              .text = labels[i],
              .position = {.x = x + ui::cursor_advance(r, style), .y = y},
              .char_size = style.font_size_body,
              .colour = style.choice,
          });
        }
        y += line_h;
      }
    }

  } // namespace

  void loot_panel_render(platform::Renderer &r, const ui::PanelStyle &style, const ui::NinePatchBorder &border,
                         std::string_view container_name, const std::vector<InventoryLine> &container,
                         const std::vector<InventoryLine> &player, const LootState &state, core::math::Vec2 viewport,
                         input::InputDevice last_device) {
    constexpr float k_pad = 20.f;
    constexpr float k_column_gap = 24.f;
    constexpr std::string_view k_player_title = "Inventory";
    constexpr float k_split_frac = 0.5f;

    const float line_h = std::max(style.line_spacing, static_cast<float>(style.font_size_body) + 6.f);
    const float title_h = std::max(line_h, static_cast<float>(style.font_size_speaker) + 6.f);
    const float cursor_w = ui::cursor_advance(r, style);

    const std::vector<std::string> container_labels = row_labels(container);
    const std::vector<std::string> player_labels = row_labels(player);

    const std::string footer = std::format(
        "{} Take/Give   {} Switch   {} Close", ui::input_glyph(input::Action::Activate, last_device),
        ui::input_glyph(input::Action::MoveLeft, last_device), ui::input_glyph(input::Action::Cancel, last_device));

    const float column_w = std::max(r.measure_text(style.font_id, container_name, style.font_size_speaker),
                                    r.measure_text(style.font_id, k_player_title, style.font_size_speaker));
    float widest = std::max(column_w, r.measure_text(style.font_id, footer, style.font_size_body));
    for (const std::string &label : container_labels)
      widest = std::max(widest, cursor_w + r.measure_text(style.font_id, label, style.font_size_body));
    for (const std::string &label : player_labels)
      widest = std::max(widest, cursor_w + r.measure_text(style.font_id, label, style.font_size_body));

    const float panel_w = std::max(440.f, (widest * 2.f) + k_column_gap + (k_pad * 2.f));
    const auto rows = std::max<std::size_t>({container_labels.size(), player_labels.size(), 1});
    const float panel_h = (k_pad * 2.f) + title_h + k_pad + (static_cast<float>(rows) * line_h) + k_pad + line_h;
    const float panel_x = (viewport.x - panel_w) * 0.5f;
    const float panel_y = (viewport.y - panel_h) * 0.5f;

    ui::panel_chrome(r, style.bg, border, {.x = panel_x, .y = panel_y}, {.x = panel_w, .y = panel_h});

    const float left_w = (panel_w - (k_pad * 2.f) - k_column_gap) * k_split_frac;
    const float left_x = panel_x + k_pad;
    const float right_x = left_x + left_w + k_column_gap;
    const float body_top = panel_y + k_pad + title_h + k_pad;

    const bool container_active = state.pane == LootPane::Container;
    r.draw(platform::DrawText{
        .font_id = style.font_id,
        .text = container_name,
        .position = {.x = left_x, .y = panel_y + k_pad},
        .char_size = style.font_size_speaker,
        .colour = container_active ? style.selected : style.speaker,
    });
    r.draw(platform::DrawText{
        .font_id = style.font_id,
        .text = k_player_title,
        .position = {.x = right_x, .y = panel_y + k_pad},
        .char_size = style.font_size_speaker,
        .colour = !container_active ? style.selected : style.speaker,
    });

    draw_pane(r, style, container_labels, left_x, body_top, container_active, state.cursor, line_h);
    draw_pane(r, style, player_labels, right_x, body_top, !container_active, state.cursor, line_h);

    r.draw(platform::DrawText{
        .font_id = style.font_id,
        .text = footer,
        .position =
            {
                .x = panel_x + ((panel_w - r.measure_text(style.font_id, footer, style.font_size_body)) * 0.5f),
                .y = panel_y + panel_h - k_pad - line_h,
            },
        .char_size = style.font_size_body,
        .colour = style.choice,
    });
  }

} // namespace corundum::gameplay::screens
