// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/gameplay/screens/barter.hpp>
#include <corundum/ui/font_family.hpp>

#include "gameplay/item_counts.hpp"
#include "gameplay/screens/screen_handlers.hpp"
#include "gameplay/screens/screen_input.hpp"

#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/gameplay.hpp>
#include <corundum/gameplay/item/item.hpp>
#include <corundum/gameplay/item/registry.hpp>
#include <corundum/gameplay/screens/hud_strip.hpp>
#include <corundum/gameplay/screens/modes.hpp>
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

/** Display label for one barter row. */
std::string barter_row_label(const BarterLine &line) {
  return std::format("{}  {} g", line.name, line.unit_price);
}

/** Footer hint, using the last-used device's glyphs. */
std::string barter_footer(input::InputDevice last_device) {
  return std::format("{} Buy   {} Close", ui::input_glyph(input::Action::Activate, last_device),
                     ui::input_glyph(input::Action::Cancel, last_device));
}

int base_price_of(const shop::StockEntry &entry, const item::Registry &items) {
  if (entry.price > 0)
    return entry.price;
  const item::Item *definition = items.find(entry.item);
  return definition != nullptr ? definition->price : 0;
}

} // namespace

std::vector<BarterLine> build_barter_stock(const shop::Shop &shop, const item::Registry &items) {
  std::vector<BarterLine> lines;
  lines.reserve(shop.stock.size());
  for (const shop::StockEntry &entry : shop.stock) {
    const item::Item *definition = items.find(entry.item);
    lines.push_back(BarterLine{
        .id = entry.item,
        .name = definition != nullptr ? definition->name : entry.item,
        .unit_price = base_price_of(entry, items),
    });
  }
  return lines;
}

BarterLayout barter_panel_layout(const platform::Renderer &renderer, const ui::PanelStyle &style,
                                 std::string_view shop_name, int gold, const std::vector<BarterLine> &lines,
                                 core::math::Vec2 viewport, input::InputDevice last_device) {
  const float line_h = std::max(style.line_spacing, static_cast<float>(style.font_size_body) + 6.f);
  const float title_h = std::max(line_h, static_cast<float>(style.font_size_speaker) + 6.f);
  const float cursor_w = ui::cursor_advance(renderer, style);

  const std::string gold_line = std::format("Gold: {}", gold);
  const std::string footer = barter_footer(last_device);
  const platform::FontId font_id = style.family(ui::FontRole::Ui).get(ui::FontStyle::Regular);

  float widest = std::max({
      renderer.measure_text(font_id, shop_name, style.font_size_speaker),
      renderer.measure_text(font_id, gold_line, style.font_size_body),
      renderer.measure_text(font_id, footer, style.font_size_body),
      renderer.measure_text(font_id, k_barter_empty, style.font_size_body),
  });
  for (const BarterLine &line : lines)
    widest = std::max(widest, cursor_w + renderer.measure_text(font_id, barter_row_label(line), style.font_size_body));

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

void barter_panel_render(platform::Renderer &renderer, const ui::PanelStyle &style, const ui::NinePatchBorder &border,
                         std::string_view shop_name, int gold, const std::vector<BarterLine> &lines,
                         const BarterState &state, core::math::Vec2 viewport, input::InputDevice last_device) {
  const BarterLayout layout = barter_panel_layout(renderer, style, shop_name, gold, lines, viewport, last_device);
  const float line_h = layout.rows.row_height;

  const std::string gold_line = std::format("Gold: {}", gold);
  const std::string footer = barter_footer(last_device);
  const platform::FontId font_id = style.family(ui::FontRole::Ui).get(ui::FontStyle::Regular);

  ui::panel_chrome(renderer, style.bg, border, layout.panel_pos, layout.panel_size);

  renderer.draw(platform::DrawText{
      .font_id = font_id,
      .text = shop_name,
      .position = {.x = layout.panel_pos.x + k_barter_pad_x, .y = layout.panel_pos.y + k_barter_pad_y},
      .char_size = style.font_size_speaker,
      .colour = style.speaker,
  });
  renderer.draw(platform::DrawText{
      .font_id = font_id,
      .text = gold_line,
      .position =
          {
              .x = layout.panel_pos.x + layout.panel_size.x - k_barter_pad_x -
                   renderer.measure_text(font_id, gold_line, style.font_size_body),
              .y = layout.panel_pos.y + k_barter_pad_y,
          },
      .char_size = style.font_size_body,
      .colour = style.choice,
  });

  renderer.draw(platform::DrawText{
      .font_id = font_id,
      .text = footer,
      .position =
          {
              .x = layout.panel_pos.x +
                   ((layout.panel_size.x - renderer.measure_text(font_id, footer, style.font_size_body)) * 0.5f),
              .y = layout.panel_pos.y + layout.panel_size.y - k_barter_pad_y - line_h,
          },
      .char_size = style.font_size_body,
      .colour = style.choice,
  });

  float y = layout.rows.row_pos.y;
  if (lines.empty()) {
    renderer.draw(platform::DrawText{
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
    ui::draw_option(renderer, style, label, {.x = layout.rows.row_pos.x, .y = y}, std::cmp_equal(i, clamped_cursor));
    y += line_h;
  }
}

namespace {

/** Apply mouse intent to the barter screen: a hovered row takes focus on a real mouse move or
 *  click, and the wheel moves one row per notch.
 */
void apply_barter_pointer(const Engine &engine, BarterState &state, const input::InputIntent &intent,
                          const shop::Shop &shop, const std::vector<BarterLine> &lines) {
  if (!pointer_active(intent))
    return;

  const BarterLayout layout = barter_panel_layout(engine.renderer(), engine.render.panel_skin.style, shop.name,
                                                  world::visit_count(engine.flags, k_gold_flag), lines,
                                                  screen_viewport(engine), engine.input_mapper.last_device());
  if (pointer_focus(intent)) {
    if (const int hovered = ui::hovered_row(intent_cursor(intent), layout.rows); hovered >= 0)
      state.cursor = hovered;
  }
  if (const int steps = ui::scroll_row_delta(intent.scroll_y); steps != 0 && !lines.empty())
    state.cursor = wrap_cursor(state.cursor, steps, static_cast<int>(lines.size()));
}

/** Buy one of the highlighted barter row's item with the player's gold. */
void buy_barter_row(Engine &engine, const BarterLine &selected) {
  const int gold = world::visit_count(engine.flags, k_gold_flag);
  if (selected.unit_price > gold) {
    engine.notify("Not enough gold");
    return;
  }
  adjust_flag(engine.flags, item_flag_key(selected.id), 1);
  engine.flags[k_gold_flag] = gold - selected.unit_price;
}

} // namespace

void update_barter(Engine &engine, Gameplay &gameplay, const input::InputIntent &intent) {
  if (intent.back) {
    gameplay.active_shop_id.clear();
    engine.scene.ui.pop();
    return;
  }

  const shop::Shop *shop = gameplay.shops.find(gameplay.active_shop_id);
  if (shop == nullptr) {
    engine.scene.ui.pop();
    return;
  }

  const std::vector<BarterLine> lines = build_barter_stock(*shop, gameplay.items);
  const int rows = static_cast<int>(lines.size());
  apply_barter_pointer(engine, gameplay.barter_screen, intent, *shop, lines);

  if (intent.navigate_y != 0 && rows > 0)
    gameplay.barter_screen.cursor = wrap_cursor(gameplay.barter_screen.cursor, intent.navigate_y, rows);
  if (!intent.activate || rows == 0)
    return;

  const BarterLine &selected = lines[static_cast<std::size_t>(std::clamp(gameplay.barter_screen.cursor, 0, rows - 1))];
  buy_barter_row(engine, selected);
}

void render_barter(const Engine &engine, const Gameplay &gameplay, platform::Renderer &renderer,
                   core::math::Vec2 viewport) {
  if (engine.scene.mode() != Barter)
    return;
  const shop::Shop *shop = gameplay.shops.find(gameplay.active_shop_id);
  if (shop == nullptr)
    return;
  const std::vector<BarterLine> lines = build_barter_stock(*shop, gameplay.items);
  barter_panel_render(renderer, engine.render.panel_skin.style, engine.render.panel_skin.border, shop->name,
                      world::visit_count(engine.flags, k_gold_flag), lines, gameplay.barter_screen, viewport,
                      engine.input_mapper.last_device());
}

} // namespace corundum::gameplay::screens
