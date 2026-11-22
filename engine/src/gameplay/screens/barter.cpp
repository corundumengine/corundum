// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/gameplay/screens/barter.hpp>
#include <corundum/ui/font_family.hpp>

#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/item/item.hpp>
#include <corundum/gameplay/item/registry.hpp>
#include <corundum/gameplay/shop/shop.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/platform/resource_ids.hpp>
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

    constexpr float k_barter_min_w = 340.f;
    constexpr float k_barter_pad_x = 24.f;
    constexpr float k_barter_pad_y = 16.f;
    constexpr float k_barter_gap = 10.f;
    constexpr std::string_view k_barter_empty = "(nothing)";

    /// Display label for one barter row.
    std::string barter_row_label(const BarterLine &line) {
      return std::format("{}  {} g", line.name, line.unit_price);
    }

    /// Footer hint, using the last-used device's glyphs.
    std::string barter_footer(input::InputDevice last_device) {
      return std::format("{} Buy   {} Close", ui::input_glyph(input::Action::Activate, last_device),
                         ui::input_glyph(input::Action::Cancel, last_device));
    }

    int base_price_of(const gameplay::shop::StockEntry &entry, const gameplay::item::Registry &items) {
      if (entry.price > 0)
        return entry.price;
      const gameplay::item::Item *definition = items.find(entry.item);
      return definition != nullptr ? definition->price : 0;
    }

  } // namespace

  std::vector<BarterLine> build_barter_stock(const gameplay::shop::Shop &shop, const gameplay::item::Registry &items) {
    std::vector<BarterLine> lines;
    lines.reserve(shop.stock.size());
    for (const gameplay::shop::StockEntry &entry : shop.stock) {
      const gameplay::item::Item *definition = items.find(entry.item);
      lines.push_back(BarterLine{
          .id = entry.item,
          .name = definition != nullptr ? definition->name : entry.item,
          .unit_price = base_price_of(entry, items),
      });
    }
    return lines;
  }

  BarterLayout barter_panel_layout(const platform::Renderer &r, const ui::PanelStyle &style, std::string_view shop_name,
                                   int gold, const std::vector<BarterLine> &lines, core::math::Vec2 viewport,
                                   input::InputDevice last_device) {
    const float line_h = std::max(style.line_spacing, static_cast<float>(style.font_size_body) + 6.f);
    const float title_h = std::max(line_h, static_cast<float>(style.font_size_speaker) + 6.f);
    const float cursor_w = ui::cursor_advance(r, style);

    const std::string gold_line = std::format("Gold: {}", gold);
    const std::string footer = barter_footer(last_device);
    const platform::FontId font_id = style.family(ui::FontRole::Ui).get(ui::FontStyle::Regular);

    float widest = std::max({
        r.measure_text(font_id, shop_name, style.font_size_speaker),
        r.measure_text(font_id, gold_line, style.font_size_body),
        r.measure_text(font_id, footer, style.font_size_body),
        r.measure_text(font_id, k_barter_empty, style.font_size_body),
    });
    for (const BarterLine &line : lines)
      widest = std::max(widest, cursor_w + r.measure_text(font_id, barter_row_label(line), style.font_size_body));

    const float panel_w = std::max(k_barter_min_w, widest + (k_barter_pad_x * 2.f));
    const std::size_t rows = lines.empty() ? 1 : lines.size();
    const float rows_height = static_cast<float>(rows) * line_h;
    const float panel_h = (k_barter_pad_y * 2.f) + title_h + k_barter_gap + rows_height + k_barter_gap + line_h;
    const float panel_x = (viewport.x - panel_w) * 0.5f;
    const float panel_y = (viewport.y - panel_h) * 0.5f;

    BarterLayout layout{};
    layout.panel_pos = {.x = panel_x, .y = panel_y};
    layout.panel_size = {.x = panel_w, .y = panel_h};
    layout.rows = ui::ListHit{
        .row_pos = {.x = panel_x + k_barter_pad_x, .y = panel_y + k_barter_pad_y + title_h + k_barter_gap},
        .row_width = panel_w - (k_barter_pad_x * 2.f),
        .row_height = line_h,
        .first_row = 0,
        .visible_rows = static_cast<int>(lines.size()),
    };
    return layout;
  }

  void barter_panel_render(platform::Renderer &r, const ui::PanelStyle &style, const ui::NinePatchBorder &border,
                           std::string_view shop_name, int gold, const std::vector<BarterLine> &lines,
                           const BarterState &state, core::math::Vec2 viewport, input::InputDevice last_device) {
    const BarterLayout layout = barter_panel_layout(r, style, shop_name, gold, lines, viewport, last_device);
    const float line_h = layout.rows.row_height;

    const std::string gold_line = std::format("Gold: {}", gold);
    const std::string footer = barter_footer(last_device);
    const platform::FontId font_id = style.family(ui::FontRole::Ui).get(ui::FontStyle::Regular);

    ui::panel_chrome(r, style.bg, border, layout.panel_pos, layout.panel_size);

    r.draw(platform::DrawText{
        .font_id = font_id,
        .text = shop_name,
        .position = {.x = layout.panel_pos.x + k_barter_pad_x, .y = layout.panel_pos.y + k_barter_pad_y},
        .char_size = style.font_size_speaker,
        .colour = style.speaker,
    });
    r.draw(platform::DrawText{
        .font_id = font_id,
        .text = gold_line,
        .position =
            {
                .x = layout.panel_pos.x + layout.panel_size.x - k_barter_pad_x -
                     r.measure_text(font_id, gold_line, style.font_size_body),
                .y = layout.panel_pos.y + k_barter_pad_y,
            },
        .char_size = style.font_size_body,
        .colour = style.choice,
    });

    r.draw(platform::DrawText{
        .font_id = font_id,
        .text = footer,
        .position =
            {
                .x = layout.panel_pos.x +
                     ((layout.panel_size.x - r.measure_text(font_id, footer, style.font_size_body)) * 0.5f),
                .y = layout.panel_pos.y + layout.panel_size.y - k_barter_pad_y - line_h,
            },
        .char_size = style.font_size_body,
        .colour = style.choice,
    });

    float y = layout.rows.row_pos.y;
    if (lines.empty()) {
      r.draw(platform::DrawText{
          .font_id = font_id,
          .text = k_barter_empty,
          .position = {.x = layout.rows.row_pos.x, .y = y},
          .char_size = style.font_size_body,
          .colour = style.choice,
      });
      return;
    }

    const int clamped_cursor = std::clamp(state.cursor, 0, static_cast<int>(lines.size()) - 1);
    for (std::size_t i = 0; i < lines.size(); ++i) {
      const std::string label = barter_row_label(lines[i]);
      ui::draw_option(r, style, label, {.x = layout.rows.row_pos.x, .y = y}, std::cmp_equal(i, clamped_cursor));
      y += line_h;
    }
  }

} // namespace corundum::gameplay::screens
