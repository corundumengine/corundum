// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/gameplay/screens/combat_hud.hpp>

#include "gameplay/screens/screen_handlers.hpp"

#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/gameplay.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/platform/resource_ids.hpp>
#include <corundum/render/render_state.hpp>
#include <corundum/ui/font_family.hpp>
#include <corundum/ui/input_glyph.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/ui/ui_draw.hpp>
#include <corundum/world/map_view.hpp>
#include <corundum/world/picking.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace corundum::gameplay::screens {

namespace {

/** The rows panel's minimum content width, so the panel doesn't resize as Health numbers
 *  change. A longer row still widens it.
 */
constexpr float k_min_row_width = 300.f;
constexpr float k_pad = 12.f;
constexpr float k_bar_height = 6.f;
constexpr float k_bar_gap = 4.f;
constexpr float k_row_gap = 10.f;
constexpr float k_command_gap = 28.f;

constexpr core::math::Colour k_bar_back{.r = 60, .g = 60, .b = 60, .a = 255};
constexpr core::math::Colour k_dimmed{.r = 110, .g = 110, .b = 110, .a = 255};
constexpr core::math::Colour k_health_fill{.r = 173, .g = 56, .b = 76, .a = 255};

float body_line_height(const ui::PanelStyle &style) noexcept {
  return std::max(style.line_spacing, static_cast<float>(style.font_size_body) + 4.f);
}

/** A panel's inset: content must clear the nine-patch corners, whose cells draw at natural size. */
core::math::Vec2 panel_padding(const ui::NinePatchBorder &border) noexcept {
  return {
      .x = std::max(k_pad, static_cast<float>(border.tile_w)),
      .y = std::max(k_pad, static_cast<float>(border.tile_h)),
  };
}

/** A bar's fill fraction, clamped to [0, 1]; 0 when `whole` isn't positive. */
float fraction(int part, int whole) noexcept {
  if (whole <= 0)
    return 0.f;
  return std::clamp(static_cast<float>(part) / static_cast<float>(whole), 0.f, 1.f);
}

void draw_rect(platform::Renderer &renderer, core::math::Vec2 pos, core::math::Vec2 size, core::math::Colour colour) {
  renderer.draw(platform::DrawRect{.position = pos, .size = size, .colour = colour});
}

void draw_text(platform::Renderer &renderer, const ui::PanelStyle &style, std::string_view text, core::math::Vec2 pos,
               unsigned char_size, core::math::Colour colour) {
  renderer.draw(platform::DrawText{
      .font_id = style.family(ui::FontRole::Ui).get(ui::FontStyle::Regular),
      .text = text,
      .position = pos,
      .char_size = char_size,
      .colour = colour,
  });
}

/** One combatant's heading: its name and Health. */
std::string row_text(const CombatHudRow &row) {
  return std::format("{}  {} / {}", row.name, row.health.current, row.health.max);
}

/** A row's name colour: the selected colour for the fight's target, else the hostile's own. */
core::math::Colour row_colour(const ui::PanelStyle &style, const CombatHudRow &row) noexcept {
  if (row.is_target)
    return style.selected;
  return row.is_hostile ? style.speaker : style.body;
}

/** Draw one combatant's row from the top of its cell and return the y below it. */
float draw_row(platform::Renderer &renderer, const ui::PanelStyle &style, const CombatHudRow &row,
               std::string_view text, core::math::Vec2 pos, float bar_width) {
  const float x = pos.x;
  float y = pos.y;
  draw_text(renderer, style, text, pos, style.font_size_body, row_colour(style, row));
  y += body_line_height(style);

  y += k_bar_gap;
  const ui::RowRect health{.pos = {.x = x, .y = y}, .width = bar_width, .height = k_bar_height};
  draw_rect(renderer, health.pos, {.x = health.width, .y = health.height}, k_bar_back);
  draw_rect(renderer, health.pos,
            {.x = health.width * fraction(row.health.current, row.health.max), .y = health.height}, k_health_fill);
  y += k_bar_height;
  return y;
}

/** The panel at the top-left: one row per combatant, as wide as its longest row needs. */
void draw_rows_panel(platform::Renderer &renderer, const ui::PanelStyle &style, const ui::NinePatchBorder &border,
                     std::span<const CombatHudRow> rows) {
  const platform::FontId font = style.family(ui::FontRole::Ui).get(ui::FontStyle::Regular);
  std::vector<std::string> texts;
  texts.reserve(rows.size());
  float rows_width = k_min_row_width;
  float rows_height = 0.f;
  for (const CombatHudRow &row : rows) {
    texts.push_back(row_text(row));
    rows_width = std::max(rows_width, renderer.measure_text(font, texts.back(), style.font_size_body));
    rows_height += body_line_height(style) + k_bar_gap + k_bar_height;
  }
  if (!rows.empty())
    rows_height += k_row_gap * static_cast<float>(rows.size() - 1);

  const core::math::Vec2 pad = panel_padding(border);
  const core::math::Vec2 panel_pos{.x = style.margin, .y = style.margin};
  const core::math::Vec2 panel_size{.x = rows_width + (2.f * pad.x), .y = rows_height + (2.f * pad.y)};
  ui::panel_chrome(renderer, style.bg, border, panel_pos, panel_size);

  float y = panel_pos.y + pad.y;
  for (std::size_t i = 0; i < rows.size(); ++i) {
    y = draw_row(renderer, style, rows[i], texts[i], {.x = panel_pos.x + pad.x, .y = y}, rows_width);
    if (i + 1 < rows.size())
      y += k_row_gap;
  }
}

/** One item on a command-bar line: its text, and the colour it draws in. */
struct BarItem {
  std::string text{};

  core::math::Colour colour{};
};

/** The width of `items`' texts and the gaps between them. */
float items_width(const platform::Renderer &renderer, platform::FontId font, const ui::PanelStyle &style,
                  std::span<const BarItem> items) {
  float width = 0.f;
  for (const BarItem &item : items)
    width += renderer.measure_text(font, item.text, style.font_size_body);
  if (!items.empty())
    width += k_command_gap * static_cast<float>(items.size() - 1);
  return width;
}

/** Draw `items` in a row from `left`. */
void draw_items(platform::Renderer &renderer, const ui::PanelStyle &style, platform::FontId font,
                std::span<const BarItem> items, float left, float y) {
  float x = left;
  for (const BarItem &item : items) {
    draw_text(renderer, style, item.text, {.x = x, .y = y}, style.font_size_body, item.colour);
    x += renderer.measure_text(font, item.text, style.font_size_body) + k_command_gap;
  }
}

/** One command-bar slot's text: the device's glyph, then the command's label. A gamepad draws a
 *  slot off the shown page with its label only.
 */
std::string command_text(const CombatHudCommand &command, input::InputDevice device) {
  if (device != input::InputDevice::Gamepad)
    return std::format("{} {}", ui::input_glyph(command.action, device), command.label);
  if (!command.gamepad_action)
    return command.label;
  return std::format("{} {}", ui::input_glyph(*command.gamepad_action, device), command.label);
}

/** A slot's colour: highlighted when queued, dimmed when unavailable, else the body colour. */
core::math::Colour command_colour(const ui::PanelStyle &style, const CombatHudCommand &command) noexcept {
  if (command.is_highlighted)
    return style.selected;
  return command.is_available ? style.body : k_dimmed;
}

/** The command bar along the bottom: the commands' line over a panel sized around it, with the
 *  page hint on a gamepad.
 */
void draw_command_bar(platform::Renderer &renderer, const ui::PanelStyle &style, const ui::NinePatchBorder &border,
                      const CombatHud &hud, core::math::Vec2 viewport, input::InputDevice device) {
  if (hud.commands.empty())
    return;

  const platform::FontId font = style.family(ui::FontRole::Ui).get(ui::FontStyle::Regular);
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

  const float commands_width = items_width(renderer, font, style, commands);
  const float line_height = body_line_height(style);
  const core::math::Vec2 pad = panel_padding(border);
  const float commands_top = viewport.y - style.margin - pad.y - line_height;
  const core::math::Vec2 panel_pos{.x = ((viewport.x - commands_width) / 2.f) - pad.x, .y = commands_top - pad.y};
  const core::math::Vec2 panel_size{.x = commands_width + (2.f * pad.x), .y = line_height + (2.f * pad.y)};
  ui::panel_chrome(renderer, style.bg, border, panel_pos, panel_size);

  draw_items(renderer, style, font, commands, (viewport.x - commands_width) / 2.f, commands_top);
}

void draw_banner(platform::Renderer &renderer, const ui::PanelStyle &style, core::math::Vec2 viewport,
                 std::string_view banner) {
  if (banner.empty())
    return;
  const platform::FontId font = style.family(ui::FontRole::Ui).get(ui::FontStyle::Regular);
  const float width = renderer.measure_text(font, banner, style.font_size_speaker);
  draw_text(renderer, style, banner, {.x = (viewport.x - width) / 2.f, .y = style.margin}, style.font_size_speaker,
            style.speaker);
}

} // namespace

void combat_hud_render(platform::Renderer &renderer, const ui::PanelStyle &style, const ui::NinePatchBorder &border,
                       const CombatHud &hud, core::math::Vec2 viewport, input::InputDevice device,
                       std::string_view banner) {
  draw_rows_panel(renderer, style, border, hud.rows);
  draw_command_bar(renderer, style, border, hud, viewport, device);
  draw_banner(renderer, style, viewport, banner);
}

namespace {

constexpr float k_target_line_thickness = 2.f;

/** Outline the fight's target's tile in the selected colour, through the view the world just
 *  drew with so the outline stays on the ground while the camera moves.
 */
void draw_target_marker(const Engine &engine, const combat::Battle &battle, platform::Renderer &renderer) {
  const world::MapView map = world::build_map_view(engine.render, engine.config);
  const std::optional<world::TileCoord> target_tile = battle.target_tile();
  if (!target_tile)
    return;
  const world::TileCoord tile = *target_tile;
  const world::TileDiamond diamond =
      world::tile_diamond(tile, world::discrete_elevation_at(map, tile.col, tile.row), map.projection);
  const std::array corners{diamond.top, diamond.right, diamond.bottom, diamond.left};

  const render::WorldView &view = engine.render.world_view;
  const core::math::Colour target_colour = engine.render.panel_skin.style.selected;
  renderer.set_world_view(view.camera, view.viewport_size, view.zoom);
  for (std::size_t i = 0; i < corners.size(); ++i) {
    renderer.draw(platform::DrawLine{
        .start = corners[i],
        .end = corners[(i + 1) % corners.size()],
        .colour = target_colour,
        .thickness = k_target_line_thickness,
    });
  }
  renderer.reset_screen_view();
}

} // namespace

void render_combat(const Engine &engine, const combat::Battle &battle, platform::Renderer &renderer,
                   core::math::Vec2 viewport, std::string_view banner) {
  draw_target_marker(engine, battle, renderer);
  combat_hud_render(renderer, engine.render.panel_skin.style, engine.render.panel_skin.border, battle.hud(), viewport,
                    engine.input_mapper.last_device(), banner);
}

std::string paused_banner(input::InputDevice device) {
  return std::format("Paused: {} to resume", ui::input_glyph(input::Action::Pause, device));
}

} // namespace corundum::gameplay::screens
