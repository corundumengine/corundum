// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/gameplay/screens/hub_tabs.hpp>
#include <corundum/gameplay/screens/modes.hpp>
#include <corundum/ui/font_family.hpp>

#include "gameplay/screens/screen_handlers.hpp"
#include "gameplay/screens/screen_input.hpp"

#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/gameplay.hpp>
#include <corundum/gameplay/screens/codex.hpp>
#include <corundum/gameplay/screens/inventory_panel.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/platform/resource_ids.hpp>
#include <corundum/ui/input_glyph.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/world/ui_stack.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>

namespace corundum::gameplay::screens {

namespace {

constexpr float k_top_margin = 12.f;
constexpr float k_tab_gap = 22.f;
constexpr float k_glyph_gap = 16.f;

} // namespace

std::string_view hub_tab_label(world::GameMode mode) noexcept {
  switch (mode) {
    case Inventory: return "Inventory";
    case Journal: return "Journal";
    case Codex: return "Codex";
    case Map: return "Map";
    default: return {};
  }
}

HubTabStrip hub_tab_strip(const platform::Renderer &renderer, const ui::PanelStyle &style, core::math::Vec2 viewport) {
  HubTabStrip strip{};
  strip.line_height = std::max(style.line_spacing, static_cast<float>(style.font_size_body) + 4.f);
  strip.y = k_top_margin;

  std::array<float, k_hub_tab_modes.size()> widths{};
  float total = 0.f;
  const platform::FontId font_id = style.family(ui::FontRole::Ui).get(ui::FontStyle::Regular);
  for (std::size_t i = 0; i < k_hub_tab_modes.size(); ++i) {
    widths[i] = renderer.measure_text(font_id, hub_tab_label(k_hub_tab_modes[i]), style.font_size_body);
    total += widths[i];
  }
  total += k_tab_gap * static_cast<float>(k_hub_tab_modes.size() - 1);

  float x = (viewport.x - total) * 0.5f;
  for (std::size_t i = 0; i < k_hub_tab_modes.size(); ++i) {
    strip.tabs[i] = HubTabRect{.pos = {.x = x, .y = strip.y}, .width = widths[i]};
    x += widths[i] + k_tab_gap;
  }
  return strip;
}

int hub_tab_at(const HubTabStrip &strip, core::math::Vec2 cursor) noexcept {
  if (cursor.y < strip.y || cursor.y > strip.y + strip.line_height)
    return -1;
  for (std::size_t i = 0; i < strip.tabs.size(); ++i) {
    const HubTabRect &tab = strip.tabs[i];
    if (cursor.x >= tab.pos.x && cursor.x <= tab.pos.x + tab.width)
      return static_cast<int>(i);
  }
  return -1;
}

float hub_panel_top_inset(const ui::PanelStyle &style) noexcept {
  return k_top_margin + std::max(style.line_spacing, static_cast<float>(style.font_size_body) + 4.f);
}

void hub_tab_strip_render(platform::Renderer &renderer, const ui::PanelStyle &style, world::GameMode active,
                          core::math::Vec2 viewport, input::InputDevice last_device) {
  const HubTabStrip strip = hub_tab_strip(renderer, style, viewport);
  const platform::FontId font_id = style.family(ui::FontRole::Ui).get(ui::FontStyle::Regular);
  const std::string_view prev = ui::input_glyph(input::Action::TabPrev, last_device);
  const std::string_view next = ui::input_glyph(input::Action::TabNext, last_device);

  const float prev_w = renderer.measure_text(font_id, prev, style.font_size_prompt);
  renderer.draw(platform::DrawText{
      .font_id = font_id,
      .text = prev,
      .position = {.x = strip.tabs.front().pos.x - k_glyph_gap - prev_w, .y = strip.y},
      .char_size = style.font_size_prompt,
      .colour = style.choice,
  });

  for (std::size_t i = 0; i < k_hub_tab_modes.size(); ++i) {
    renderer.draw(platform::DrawText{
        .font_id = font_id,
        .text = hub_tab_label(k_hub_tab_modes[i]),
        .position = strip.tabs[i].pos,
        .char_size = style.font_size_body,
        .colour = k_hub_tab_modes[i] == active ? style.speaker : style.choice,
    });
  }

  const HubTabRect &last = strip.tabs.back();
  renderer.draw(platform::DrawText{
      .font_id = font_id,
      .text = next,
      .position = {.x = last.pos.x + last.width + k_glyph_gap, .y = strip.y},
      .char_size = style.font_size_prompt,
      .colour = style.choice,
  });
}

namespace {

/** One row of the menu hub's action → tab table. */
struct HubTabAction {
  input::Action action{};

  world::GameMode mode{};
};

/** The keyboard hotkeys that open a hub tab directly (I/J/C/M). */
constexpr std::array<HubTabAction, 4> k_hub_tab_actions{
    {
        {.action = input::Action::Inventory, .mode = Inventory},
        {.action = input::Action::Journal, .mode = Journal},
        {.action = input::Action::Codex, .mode = Codex},
        {.action = input::Action::Map, .mode = Map},
    },
};

/** Next hub tab relative to `mode` by `direction` (+1 / -1), wrapping. */
world::GameMode cycle_hub_tab(world::GameMode mode, int direction) noexcept {
  std::size_t index = 0;
  for (std::size_t i = 0; i < k_hub_tab_modes.size(); ++i) {
    if (k_hub_tab_modes[i] == mode)
      index = i;
  }
  const auto count = static_cast<int>(k_hub_tab_modes.size());
  const int next = (((static_cast<int>(index) + direction) % count) + count) % count;
  return k_hub_tab_modes[static_cast<std::size_t>(next)];
}

/** Reset a hub tab's open-time state: its cursor, and any cache the tab owns. */
void open_hub_tab(const Engine &engine, Gameplay &gameplay, world::GameMode mode) {
  gameplay.last_hub_mode = mode;
  switch (mode) {
    case Inventory:
      gameplay.inventory_cursor = 0;
      gameplay.inventory_scroll = 0;
      // Built once here rather than every render frame: the inventory is read-only and the
      // simulation is paused while it is open, so there is no mutation to invalidate it.
      gameplay.inventory_lines = build_inventory_lines(engine.flags, gameplay.items);
      gameplay.inventory_equipment = build_equipment_lines(engine.flags, gameplay.items);
      break;
    case Journal:
      gameplay.journal_screen.cursor = 0;
      gameplay.journal_screen.scroll = 0;
      break;
    case Codex:
      gameplay.codex_screen.cursor = 0;
      gameplay.codex_screen.scroll = 0.f;
      codex_mark_dirty(gameplay.codex_screen);
      refresh_codex(gameplay.codex_screen, gameplay.codex, engine.flags);
      break;
    case Map:
      gameplay.map_screen.cursor = 0;
      gameplay.map_screen.scroll = 0;
      break;
    default: break;
  }
}

/** Replace the top hub layer with `mode` (tab switch), reselecting that tab's state. */
void switch_hub_tab(Engine &engine, Gameplay &gameplay, world::GameMode mode) {
  engine.scene.ui.pop();
  engine.scene.ui.push(mode);
  open_hub_tab(engine, gameplay, mode);
}

/** Handle a click or wheel over the hub tab strip. Returns true when the step was consumed. */
bool handle_hub_strip_pointer(Engine &engine, Gameplay &gameplay, const input::InputIntent &intent) {
  if (!pointer_active(intent))
    return false;

  const HubTabStrip strip = hub_tab_strip(engine.renderer(), engine.render.panel_skin.style, screen_viewport(engine));
  const int tab = hub_tab_at(strip, intent_cursor(intent));
  if (tab < 0)
    return false;

  const world::GameMode mode = k_hub_tab_modes[static_cast<std::size_t>(tab)];
  if (intent.cursor_clicked) {
    if (mode != engine.scene.mode())
      switch_hub_tab(engine, gameplay, mode);
    return true;
  }
  if (intent.scroll_y != 0.f) {
    const int direction = intent.scroll_y > 0.f ? -1 : 1;
    switch_hub_tab(engine, gameplay, cycle_hub_tab(engine.scene.mode(), direction));
    return true;
  }
  return false;
}

/** Handle the Hub button: toggle the last-used tab on a hub, open it from Exploring. */
bool handle_hub_button(Engine &engine, Gameplay &gameplay, bool on_hub_tab) {
  if (!engine.input_state.is_pressed(input::Action::Hub))
    return false;
  if (on_hub_tab) {
    engine.scene.ui.pop();
    return true;
  }
  if (engine.scene.mode() != world::GameMode::Exploring)
    return false;
  open_hub_tab(engine, gameplay, gameplay.last_hub_mode);
  engine.scene.ui.push(gameplay.last_hub_mode);
  return true;
}

/** Handle the I/J/C/M hotkeys: toggle their tab on a hub, open it from Exploring. */
bool handle_hub_hotkeys(Engine &engine, Gameplay &gameplay, bool on_hub_tab) {
  for (const HubTabAction &row : k_hub_tab_actions) {
    if (!engine.input_state.is_pressed(row.action))
      continue;
    if (engine.scene.mode() == row.mode) {
      engine.scene.ui.pop();
      return true;
    }
    if (on_hub_tab) {
      switch_hub_tab(engine, gameplay, row.mode);
      return true;
    }
    if (engine.scene.mode() != world::GameMode::Exploring)
      continue; // Another screen (dialogue, prompt, menu, loot, barter) is on top.
    open_hub_tab(engine, gameplay, row.mode);
    engine.scene.ui.push(row.mode);
    return true;
  }
  return false;
}

} // namespace

/** The gameplay framework's pre-dispatch input hook: handles the menu hub's open/close/switch
 *  and tab cycling. Returns true when it consumed the step; safe to register as the only
 *  entry in Engine::screen_input_hooks.
 */
bool handle_hub_input(Engine &engine, Gameplay &gameplay, const input::InputIntent &intent) {
  // The menu hub: one gamepad button (Hub) toggles the last-used tab, and the I/J/C/M hotkeys
  // select a tab directly. The hub is not a GameMode of its own — it is the convention that the
  // top of the UI stack is one of Inventory/Journal/Codex/Map. Opening, switching or closing
  // consumes the step so the same press cannot also act on the fresh screen.
  const bool on_hub_tab = is_hub_mode(engine.scene.mode());
  if (handle_hub_button(engine, gameplay, on_hub_tab) || handle_hub_hotkeys(engine, gameplay, on_hub_tab))
    return true;

  // While a hub tab is on top, the tab strip is directly clickable and the wheel over it
  // cycles tabs; a click on the active tab is a no-op.
  if (on_hub_tab && handle_hub_strip_pointer(engine, gameplay, intent))
    return true;

  // While a hub tab is on top, TabNext/TabPrev cycle the four tabs by replacing the top layer;
  // the engine dispatches the newly active screen's step after this hook returns false.
  if (on_hub_tab && (intent.next_tab || intent.prev_tab))
    switch_hub_tab(engine, gameplay, cycle_hub_tab(engine.scene.mode(), intent.next_tab ? 1 : -1));
  return false;
}

/** The hub tab strip draws over every hub panel, in the top margin so it never overlaps one. */
void render_hub_strip(const Engine &engine, platform::Renderer &renderer, core::math::Vec2 viewport) {
  if (!is_hub_mode(engine.scene.mode()))
    return;
  hub_tab_strip_render(renderer, engine.render.panel_skin.style, engine.scene.mode(), viewport,
                       engine.input_mapper.last_device());
}

} // namespace corundum::gameplay::screens
