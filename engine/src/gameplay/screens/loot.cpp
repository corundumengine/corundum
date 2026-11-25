// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/gameplay/screens/loot.hpp>
#include <corundum/ui/font_family.hpp>

#include "gameplay/item_counts.hpp"
#include "gameplay/screens/screen_handlers.hpp"
#include "gameplay/screens/screen_input.hpp"

#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/gameplay.hpp>
#include <corundum/gameplay/item/container.hpp>
#include <corundum/gameplay/item/item.hpp>
#include <corundum/gameplay/screens/inventory_panel.hpp>
#include <corundum/gameplay/screens/modes.hpp>
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

std::vector<std::string> row_labels(const std::vector<InventoryLine> &lines) {
  std::vector<std::string> labels;
  labels.reserve(lines.size());
  for (const InventoryLine &line : lines)
    labels.push_back(std::format("{}  x{}", line.name, line.count));
  return labels;
}

/** Draw one pane's rows; the active pane uses ui::draw_option (cursor + selected colour), the
 *  inactive one plain text.
 */
void draw_pane(platform::Renderer &renderer, const ui::PanelStyle &style, const std::vector<std::string> &labels,
               float x, float y, bool active, int cursor, float line_h) {
  constexpr std::string_view k_empty = "(empty)";
  const platform::FontId font_id = style.family(ui::FontRole::Ui).get(ui::FontStyle::Regular);
  if (labels.empty()) {
    renderer.draw(platform::DrawText{
        .font_id = font_id,
        .text = k_empty,
        .position = {.x = x, .y = y},
        .char_size = style.font_size_body,
        .colour = style.choice,
    });
    return;
  }
  for (std::size_t i = 0; i < labels.size(); ++i) {
    if (active) {
      ui::draw_option(renderer, style, labels[i], {.x = x, .y = y}, std::cmp_equal(i, cursor));
    } else {
      renderer.draw(platform::DrawText{
          .font_id = font_id,
          .text = labels[i],
          .position = {.x = x + ui::cursor_advance(renderer, style), .y = y},
          .char_size = style.font_size_body,
          .colour = style.choice,
      });
    }
    y += line_h;
  }
}

constexpr float k_loot_pad = 20.f;
constexpr float k_loot_column_gap = 24.f;
constexpr float k_loot_min_w = 440.f;
constexpr float k_loot_split_frac = 0.5f;
constexpr std::string_view k_loot_player_title = "Inventory";

/** Footer hint, using the last-used device's glyphs. */
std::string loot_footer(input::InputDevice last_device) {
  return std::format("{} Take/Give   {} Switch   {} Close", ui::input_glyph(input::Action::Activate, last_device),
                     ui::input_glyph(input::Action::MoveLeft, last_device),
                     ui::input_glyph(input::Action::Cancel, last_device));
}

} // namespace

LootLayout loot_panel_layout(const platform::Renderer &renderer, const ui::PanelStyle &style,
                             std::string_view container_name, const std::vector<InventoryLine> &container,
                             const std::vector<InventoryLine> &player, core::math::Vec2 viewport,
                             input::InputDevice last_device) {
  const float line_h = std::max(style.line_spacing, static_cast<float>(style.font_size_body) + 6.f);
  const float title_h = std::max(line_h, static_cast<float>(style.font_size_speaker) + 6.f);
  const float cursor_w = ui::cursor_advance(renderer, style);

  const std::vector<std::string> container_labels = row_labels(container);
  const std::vector<std::string> player_labels = row_labels(player);

  const std::string footer = loot_footer(last_device);
  const platform::FontId font_id = style.family(ui::FontRole::Ui).get(ui::FontStyle::Regular);

  const float column_w = std::max(renderer.measure_text(font_id, container_name, style.font_size_speaker),
                                  renderer.measure_text(font_id, k_loot_player_title, style.font_size_speaker));
  float widest = std::max(column_w, renderer.measure_text(font_id, footer, style.font_size_body));
  for (const std::string &label : container_labels)
    widest = std::max(widest, cursor_w + renderer.measure_text(font_id, label, style.font_size_body));
  for (const std::string &label : player_labels)
    widest = std::max(widest, cursor_w + renderer.measure_text(font_id, label, style.font_size_body));

  const float panel_w = std::max(k_loot_min_w, (widest * 2.f) + k_loot_column_gap + (k_loot_pad * 2.f));
  const auto rows = std::max<std::size_t>({container_labels.size(), player_labels.size(), 1});
  const float panel_h =
      (k_loot_pad * 2.f) + title_h + k_loot_pad + (static_cast<float>(rows) * line_h) + k_loot_pad + line_h;
  const float panel_x = (viewport.x - panel_w) * 0.5f;
  const float panel_y = (viewport.y - panel_h) * 0.5f;
  const float left_w = (panel_w - (k_loot_pad * 2.f) - k_loot_column_gap) * k_loot_split_frac;
  const float left_x = panel_x + k_loot_pad;
  const float right_x = left_x + left_w + k_loot_column_gap;
  const float right_w = panel_x + panel_w - k_loot_pad - right_x;
  const float body_top = panel_y + k_loot_pad + title_h + k_loot_pad;

  LootLayout layout{};
  layout.panel_pos = {.x = panel_x, .y = panel_y};
  layout.panel_size = {.x = panel_w, .y = panel_h};
  layout.container_pane = ui::RowRect{
      .pos = {.x = left_x, .y = body_top},
      .width = left_w,
      .height = static_cast<float>(rows) * line_h,
  };
  layout.player_pane = ui::RowRect{
      .pos = {.x = right_x, .y = body_top},
      .width = right_w,
      .height = static_cast<float>(rows) * line_h,
  };
  layout.container_rows = ui::ListHit{
      .row_pos = {.x = left_x, .y = body_top},
      .row_width = left_w,
      .row_height = line_h,
      .first_row = 0,
      .visible_rows = static_cast<int>(container_labels.size()),
  };
  layout.player_rows = ui::ListHit{
      .row_pos = {.x = right_x, .y = body_top},
      .row_width = right_w,
      .row_height = line_h,
      .first_row = 0,
      .visible_rows = static_cast<int>(player_labels.size()),
  };
  return layout;
}

void loot_panel_render(platform::Renderer &renderer, const ui::PanelStyle &style, const ui::NinePatchBorder &border,
                       std::string_view container_name, const std::vector<InventoryLine> &container,
                       const std::vector<InventoryLine> &player, const LootState &state, core::math::Vec2 viewport,
                       input::InputDevice last_device) {
  const LootLayout layout =
      loot_panel_layout(renderer, style, container_name, container, player, viewport, last_device);
  const float line_h = layout.container_rows.row_height;

  const std::vector<std::string> container_labels = row_labels(container);
  const std::vector<std::string> player_labels = row_labels(player);

  const std::string footer = loot_footer(last_device);
  const platform::FontId font_id = style.family(ui::FontRole::Ui).get(ui::FontStyle::Regular);

  ui::panel_chrome(renderer, style.bg, border, layout.panel_pos, layout.panel_size);

  const bool container_active = state.pane == LootPane::Container;
  renderer.draw(platform::DrawText{
      .font_id = font_id,
      .text = container_name,
      .position = {.x = layout.container_pane.pos.x, .y = layout.panel_pos.y + k_loot_pad},
      .char_size = style.font_size_speaker,
      .colour = container_active ? style.selected : style.speaker,
  });
  renderer.draw(platform::DrawText{
      .font_id = font_id,
      .text = k_loot_player_title,
      .position = {.x = layout.player_pane.pos.x, .y = layout.panel_pos.y + k_loot_pad},
      .char_size = style.font_size_speaker,
      .colour = !container_active ? style.selected : style.speaker,
  });

  draw_pane(renderer, style, container_labels, layout.container_rows.row_pos.x, layout.container_rows.row_pos.y,
            container_active, state.cursor, line_h);
  draw_pane(renderer, style, player_labels, layout.player_rows.row_pos.x, layout.player_rows.row_pos.y,
            !container_active, state.cursor, line_h);

  renderer.draw(platform::DrawText{
      .font_id = font_id,
      .text = footer,
      .position =
          {
              .x = layout.panel_pos.x +
                   ((layout.panel_size.x - renderer.measure_text(font_id, footer, style.font_size_body)) * 0.5f),
              .y = layout.panel_pos.y + layout.panel_size.y - k_loot_pad - line_h,
          },
      .char_size = style.font_size_body,
      .colour = style.choice,
  });
}

namespace {

/** Switch to the hovered loot pane and focus its hovered row. */
void focus_hovered_loot_pane(Gameplay &gameplay, const LootLayout &layout, core::math::Vec2 cursor) {
  const bool over_player = cursor_in(cursor, layout.player_pane);
  if (over_player || cursor_in(cursor, layout.container_pane)) {
    const LootPane pane = over_player ? LootPane::Player : LootPane::Container;
    if (gameplay.loot_screen.pane != pane) {
      gameplay.loot_screen.pane = pane;
      gameplay.loot_screen.cursor = 0;
    }
    if (const int hovered = ui::hovered_row(cursor, over_player ? layout.player_rows : layout.container_rows);
        hovered >= 0)
      gameplay.loot_screen.cursor = hovered;
  }
}

/** Apply mouse intent to the loot screen: hovering a pane switches to it, hovering a row (on a
 *  real move or click) focuses it, and the wheel moves one row per notch.
 */
void apply_loot_pointer(const Engine &engine, Gameplay &gameplay, const input::InputIntent &intent,
                        std::string_view container_name, const std::vector<InventoryLine> &container_lines,
                        const std::vector<InventoryLine> &player_lines) {
  if (!pointer_active(intent))
    return;

  const LootLayout layout =
      loot_panel_layout(engine.renderer(), engine.render.panel_skin.style, container_name, container_lines,
                        player_lines, screen_viewport(engine), engine.input_mapper.last_device());
  const core::math::Vec2 cursor = intent_cursor(intent);

  if (pointer_focus(intent))
    focus_hovered_loot_pane(gameplay, layout, cursor);

  const int active_rows =
      static_cast<int>((gameplay.loot_screen.pane == LootPane::Container ? container_lines : player_lines).size());
  if (const int steps = ui::scroll_row_delta(intent.scroll_y); steps != 0 && active_rows > 0)
    gameplay.loot_screen.cursor = wrap_cursor(gameplay.loot_screen.cursor, steps, active_rows);
}

} // namespace

/** Step the loot screen: Back closes, Left/Right or a click switch the active pane, a hovered
 *  row takes focus on a real mouse move or click, the wheel moves one row per notch, and
 *  Activate moves one unit of the highlighted item to the other holder.
 */
void update_loot(Engine &engine, Gameplay &gameplay, const input::InputIntent &intent) {
  if (intent.back) {
    gameplay.active_container_id.clear();
    engine.scene.ui.pop();
    return;
  }

  const std::string container_name =
      gameplay.active_container_id.empty() ? std::string{"Container"} : gameplay.active_container_id;
  const std::vector<InventoryLine> container_lines =
      build_item_lines(engine.flags, gameplay.items, item::container_flag_prefix(gameplay.active_container_id));
  const std::vector<InventoryLine> player_lines = build_item_lines(engine.flags, gameplay.items, item::k_flag_prefix);

  if (intent.navigate_x != 0) {
    gameplay.loot_screen.pane =
        gameplay.loot_screen.pane == LootPane::Container ? LootPane::Player : LootPane::Container;
    gameplay.loot_screen.cursor = 0;
  }

  apply_loot_pointer(engine, gameplay, intent, container_name, container_lines, player_lines);

  const bool container_active = gameplay.loot_screen.pane == LootPane::Container;
  const std::vector<InventoryLine> &lines = container_active ? container_lines : player_lines;
  const int rows = static_cast<int>(lines.size());

  if (intent.navigate_y != 0 && rows > 0)
    gameplay.loot_screen.cursor = wrap_cursor(gameplay.loot_screen.cursor, intent.navigate_y, rows);
  if (!intent.activate || rows == 0)
    return;

  const InventoryLine &selected = lines[static_cast<std::size_t>(std::clamp(gameplay.loot_screen.cursor, 0, rows - 1))];
  const std::string container_key = item::container_item_flag_key(gameplay.active_container_id, selected.id);
  const std::string player_key = item_flag_key(selected.id);
  if (container_active) {
    adjust_flag(engine.flags, container_key, -1);
    adjust_flag(engine.flags, player_key, 1);
  } else {
    adjust_flag(engine.flags, player_key, -1);
    adjust_flag(engine.flags, container_key, 1);
  }
}

void render_loot(const Engine &engine, const Gameplay &gameplay, platform::Renderer &renderer,
                 core::math::Vec2 viewport) {
  if (engine.scene.mode() != Loot)
    return;
  const std::string container_name =
      gameplay.active_container_id.empty() ? std::string{"Container"} : gameplay.active_container_id;
  loot_panel_render(
      renderer, engine.render.panel_skin.style, engine.render.panel_skin.border, container_name,
      build_item_lines(engine.flags, gameplay.items, item::container_flag_prefix(gameplay.active_container_id)),
      build_item_lines(engine.flags, gameplay.items, item::k_flag_prefix), gameplay.loot_screen, viewport,
      engine.input_mapper.last_device());
}

} // namespace corundum::gameplay::screens
