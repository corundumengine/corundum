// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/ui/barter.hpp>

#include <corundum/core/math/vec.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/item/item.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/dialog_box.hpp>
#include <corundum/ui/input_glyph.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/ui/ui_draw.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace corundum::ui {

  namespace {

    /// Reputation is clamped to this many points of discount, so rep 50+ is the best price.
    constexpr int k_max_discount_percent = 50;

    int base_price_of(const shop::StockEntry &entry, const item::Registry &items) {
      if (entry.price > 0)
        return entry.price;
      const item::Item *definition = items.find(entry.item);
      return definition != nullptr ? definition->price : 0;
    }

  } // namespace

  int barter_buy_price(int base_price, int reputation) noexcept {
    if (base_price <= 0)
      return 0;
    const int discount = std::clamp(reputation, 0, k_max_discount_percent);
    return std::max(1, base_price - ((base_price * discount) / 100));
  }

  int barter_sell_price(int base_price, float buy_rate) noexcept {
    if (base_price <= 0)
      return 0;
    return std::max(0, static_cast<int>(std::lround(static_cast<float>(base_price) * buy_rate)));
  }

  std::vector<BarterLine> build_barter_stock(const shop::Shop &shop, const item::Registry &items, int reputation) {
    std::vector<BarterLine> lines;
    lines.reserve(shop.stock.size());
    for (const shop::StockEntry &entry : shop.stock) {
      const item::Item *definition = items.find(entry.item);
      lines.push_back(BarterLine{
          .count = 0,
          .id = entry.item,
          .name = definition != nullptr ? definition->name : entry.item,
          .unit_price = barter_buy_price(base_price_of(entry, items), reputation),
      });
    }
    return lines;
  }

  std::vector<BarterLine> build_barter_sell_lines(const shop::Shop &shop, const item::Registry &items,
                                                  const world::FlagStore &flags) {
    std::vector<BarterLine> lines;
    for (const auto &[key, count] : flags) {
      if (count <= 0 || !key.starts_with(item::k_flag_prefix))
        continue;
      std::string_view id{key};
      id.remove_prefix(item::k_flag_prefix.size());
      const item::Item *definition = items.find(id);
      if (definition == nullptr || definition->price <= 0)
        continue;
      lines.push_back(BarterLine{
          .count = count,
          .id = std::string{id},
          .name = definition->name,
          .unit_price = barter_sell_price(definition->price, shop.buy_rate),
      });
    }
    std::ranges::sort(lines, {}, [](const BarterLine &line) { return line.name; });
    return lines;
  }

  void barter_panel_render(platform::Renderer &r, const DialogBoxStyle &style, const NinePatchBorder &border,
                           std::string_view shop_name, int gold, const std::vector<BarterLine> &lines,
                           const BarterState &state, core::math::Vec2 viewport, input::InputDevice last_device) {
    constexpr float k_min_w = 340.f;
    constexpr float k_pad_x = 24.f;
    constexpr float k_pad_y = 16.f;
    constexpr float k_gap = 10.f;
    constexpr std::string_view k_empty = "(nothing)";

    const float line_h = std::max(style.line_spacing, static_cast<float>(style.font_size_body) + 6.f);
    const float title_h = std::max(line_h, static_cast<float>(style.font_size_speaker) + 6.f);
    const float cursor_w = cursor_advance(r, style);

    const std::string tabs = "Buy    Sell";
    const std::string gold_line = std::format("Gold: {}", gold);
    const std::string footer =
        std::format("{} Trade   {} Tab   {} Close", input_glyph(input::Action::Activate, last_device),
                    input_glyph(input::Action::TabNext, last_device), input_glyph(input::Action::Cancel, last_device));

    float widest = std::max({r.measure_text(style.font_id, shop_name, style.font_size_speaker),
                             r.measure_text(style.font_id, tabs, style.font_size_body),
                             r.measure_text(style.font_id, gold_line, style.font_size_body),
                             r.measure_text(style.font_id, footer, style.font_size_body),
                             r.measure_text(style.font_id, k_empty, style.font_size_body)});
    std::vector<std::string> labels;
    labels.reserve(lines.size());
    for (const BarterLine &line : lines) {
      labels.push_back(state.tab == BarterTab::Sell
                           ? std::format("{}  x{}  {} g", line.name, line.count, line.unit_price)
                           : std::format("{}  {} g", line.name, line.unit_price));
      widest = std::max(widest, cursor_w + r.measure_text(style.font_id, labels.back(), style.font_size_body));
    }

    const float panel_w = std::max(k_min_w, widest + (k_pad_x * 2.f));
    const std::size_t rows = labels.empty() ? 1 : labels.size();
    const float panel_h =
        (k_pad_y * 2.f) + title_h + k_gap + line_h + k_gap + (static_cast<float>(rows) * line_h) + k_gap + line_h;
    const float panel_x = (viewport.x - panel_w) * 0.5f;
    const float panel_y = (viewport.y - panel_h) * 0.5f;

    panel_chrome(r, style.bg, border, {.x = panel_x, .y = panel_y}, {.x = panel_w, .y = panel_h});

    r.draw(platform::DrawText{
        .font_id = style.font_id,
        .text = shop_name,
        .position = {.x = panel_x + k_pad_x, .y = panel_y + k_pad_y},
        .char_size = style.font_size_speaker,
        .colour = style.speaker,
    });
    r.draw(platform::DrawText{
        .font_id = style.font_id,
        .text = gold_line,
        .position = {.x = panel_x + panel_w - k_pad_x - r.measure_text(style.font_id, gold_line, style.font_size_body),
                     .y = panel_y + k_pad_y},
        .char_size = style.font_size_body,
        .colour = style.choice,
    });

    // Two tab labels; the active one is highlighted. Buy starts at the panel's left padding.
    const float tab_y = panel_y + k_pad_y + title_h + k_gap;
    const float buy_w = r.measure_text(style.font_id, "Buy", style.font_size_body);
    const float sell_x = panel_x + k_pad_x + buy_w + 24.f;
    r.draw(platform::DrawText{
        .font_id = style.font_id,
        .text = "Buy",
        .position = {.x = panel_x + k_pad_x, .y = tab_y},
        .char_size = style.font_size_body,
        .colour = state.tab == BarterTab::Buy ? style.selected : style.choice,
    });
    r.draw(platform::DrawText{
        .font_id = style.font_id,
        .text = "Sell",
        .position = {.x = sell_x, .y = tab_y},
        .char_size = style.font_size_body,
        .colour = state.tab == BarterTab::Sell ? style.selected : style.choice,
    });

    r.draw(platform::DrawText{
        .font_id = style.font_id,
        .text = footer,
        .position = {.x = panel_x + ((panel_w - r.measure_text(style.font_id, footer, style.font_size_body)) * 0.5f),
                     .y = panel_y + panel_h - k_pad_y - line_h},
        .char_size = style.font_size_body,
        .colour = style.choice,
    });

    float y = tab_y + line_h + k_gap;
    if (labels.empty()) {
      r.draw(platform::DrawText{
          .font_id = style.font_id,
          .text = k_empty,
          .position = {.x = panel_x + k_pad_x, .y = y},
          .char_size = style.font_size_body,
          .colour = style.choice,
      });
      return;
    }

    const int clamped_cursor = std::clamp(state.cursor, 0, static_cast<int>(labels.size()) - 1);
    for (std::size_t i = 0; i < labels.size(); ++i) {
      draw_option(r, style, labels[i], {.x = panel_x + k_pad_x, .y = y}, std::cmp_equal(i, clamped_cursor));
      y += line_h;
    }
  }

} // namespace corundum::ui
