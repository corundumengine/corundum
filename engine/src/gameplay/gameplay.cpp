// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/direction.hpp>
#include <corundum/core/math/vec.hpp>
#include <corundum/engine.hpp>
#include <corundum/entities/entity.hpp>
#include <corundum/entities/world.hpp>
#include <corundum/gameplay/codex/codex.hpp>
#include <corundum/gameplay/codex/registry.hpp>
#include <corundum/gameplay/dialogue/action.hpp>
#include <corundum/gameplay/dialogue/conversation.hpp>
#include <corundum/gameplay/dialogue/dialogue.hpp>
#include <corundum/gameplay/dialogue/dialogue_npc.hpp>
#include <corundum/gameplay/dialogue/validate_refs.hpp>
#include <corundum/gameplay/gameplay.hpp>
#include <corundum/gameplay/item/container.hpp>
#include <corundum/gameplay/item/item.hpp>
#include <corundum/gameplay/item/registry.hpp>
#include <corundum/gameplay/location/location.hpp>
#include <corundum/gameplay/quest/quest.hpp>
#include <corundum/gameplay/quest/runner.hpp>
#include <corundum/gameplay/quest/status.hpp>
#include <corundum/gameplay/quest/system.hpp>
#include <corundum/gameplay/screens/barter.hpp>
#include <corundum/gameplay/screens/codex.hpp>
#include <corundum/gameplay/screens/dialog_box.hpp>
#include <corundum/gameplay/screens/hub_tabs.hpp>
#include <corundum/gameplay/screens/hud_strip.hpp>
#include <corundum/gameplay/screens/inventory_panel.hpp>
#include <corundum/gameplay/screens/journal.hpp>
#include <corundum/gameplay/screens/loot.hpp>
#include <corundum/gameplay/screens/map.hpp>
#include <corundum/gameplay/screens/modes.hpp>
#include <corundum/gameplay/shop/shop.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/input_intent.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/render/render_state.hpp>
#include <corundum/save/save.hpp>
#include <corundum/screen_registry.hpp>
#include <corundum/sprites/sprite.hpp>
#include <corundum/ui/toast.hpp>
#include <corundum/ui/ui_draw.hpp>
#include <corundum/world/flags.hpp>
#include <corundum/world/portals/portal.hpp>
#include <corundum/world/scene.hpp>
#include <corundum/world/ui_stack.hpp>

#include "core/warn_log.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

namespace corundum::gameplay {

  namespace {

    using corundum::detail::warn_log;

    /// Parse ev.args[index] as an int; returns `fallback` if absent, unparseable, or only
    /// partially numeric.
    int event_int_arg(const dialogue::EventAction &ev, std::size_t index, int fallback) noexcept {
      if (index >= ev.args.size())
        return fallback;
      const std::string &s = ev.args[index];
      int value{fallback};
      const auto [end, error] = std::from_chars(s.data(), s.data() + s.size(), value);
      if (error != std::errc{} || end != s.data() + s.size())
        return fallback;
      return value;
    }

    void validate_quest_references(const dialogue::Registry &graphs, const quest::Registry &quests,
                                   const item::Registry &items) {
      for (const auto &[id, graph] : graphs) {
        for (const auto &err : dialogue::validate_quest_refs(graph, quests, &items, &graphs))
          warn_log("[gameplay] WARN: dialogue '{}' {}", id, err);
        for (const auto &err : dialogue::validate_condition_quest_refs(graph, quests))
          warn_log("[gameplay] WARN: dialogue '{}' {}", id, err);
      }
    }

    /// Outcome of consulting the game-provided dialogue-event hook.
    enum class EventHookResult : std::uint8_t {
      NotHandled, ///< No hook is installed, or it reported the event as unhandled.
      Handled,    ///< The hook reported the event as handled.
      Threw,      ///< The hook threw; already logged, and the event is left suppressed.
    };

    /// Invoke the game's dialogue-event hook, swallowing any exceptions so the noexcept contract
    /// on the dispatch loop holds. A throwing hook maps to Threw so dispatch does not also report
    /// the event as unknown.
    EventHookResult invoke_event_hook(const Gameplay &gameplay, Engine &engine,
                                      const dialogue::EventAction &ev) noexcept {
      if (!gameplay.on_event)
        return EventHookResult::NotHandled;
      try {
        return gameplay.on_event(engine, ev) ? EventHookResult::Handled : EventHookResult::NotHandled;
      } catch (...) {
        warn_log("[gameplay] WARN: on_event handler threw on '{}'", ev.name);
        return EventHookResult::Threw;
      }
    }

    void handle_play_sound(Engine &engine, const dialogue::EventAction &ev) {
      const auto result = engine.audio.play_sound(ev.args[0]);
      if (!result)
        warn_log("[gameplay] WARN: {}", result.error());
    }

    void handle_quest_start(Engine &engine, const Gameplay &gameplay, quest::Runner &quest_runner,
                            const dialogue::EventAction &ev) {
      const bool already_started = quest::get_stage(ev.args[0], engine.flags) > 0;
      if (auto result = quest_runner.start(ev.args[0]); !result) {
        warn_log("[gameplay] WARN: {}", result.error());
        return;
      }
      if (already_started)
        return;
      const quest::Quest *quest = gameplay.quests.find(ev.args[0]);
      if (quest != nullptr)
        engine.notify(std::format("Quest started: {}", quest->name), ui::k_toast_default_colour);
      // Auto-track the newly started quest only when nothing else is tracked, so the HUD has a
      // default without overriding a quest the player pinned by hand.
      const bool any_tracked = std::ranges::any_of(
          quest::started_quests(gameplay.quests, engine.flags), [&engine](const quest::Quest *started) {
            return started != nullptr && world::has_flag(engine.flags, quest::tracked_flag_key(started->quest_id));
          });
      if (!any_tracked)
        world::set_flag(engine.flags, quest::tracked_flag_key(ev.args[0]));
    }

    void handle_quest_advance(Engine &engine, const Gameplay &gameplay, quest::Runner &quest_runner,
                              const dialogue::EventAction &ev) {
      const int stage_before = quest::get_stage(ev.args[0], engine.flags);
      if (auto result = quest_runner.advance(ev.args[0], ev.args[1]); !result) {
        warn_log("[gameplay] WARN: {}", result.error());
        return;
      }
      // advance() reports ok even when the stage name was unknown (a logged no-op), so only
      // notify when the stage integer actually moved.
      if (quest::get_stage(ev.args[0], engine.flags) == stage_before)
        return;
      const quest::Quest *quest = gameplay.quests.find(ev.args[0]);
      if (quest == nullptr)
        return;
      switch (quest::lifecycle(*quest, engine.flags)) {
        case quest::Lifecycle::Completed:
          engine.notify(std::format("Quest complete: {}", quest->name), ui::k_toast_complete_colour);
          break;
        case quest::Lifecycle::Failed:
          engine.notify(std::format("Quest failed: {}", quest->name), ui::k_toast_failed_colour);
          break;
        case quest::Lifecycle::Active:
        case quest::Lifecycle::NotStarted:
          engine.notify(std::format("Quest updated: {}", quest->name), ui::k_toast_updated_colour);
          break;
      }
    }

    /// The FlagStore key holding an item's runtime count (`item.<id>`).
    std::string item_flag_key(std::string_view id) {
      return std::format("{}{}", item::k_flag_prefix, id);
    }

    void handle_take_item(Engine &engine, const dialogue::EventAction &ev) {
      const std::string key{item_flag_key(ev.args[0])};
      if (const auto it = engine.flags.find(key); it != engine.flags.end()) {
        it->second -= event_int_arg(ev, 1, /*fallback=*/1);
        if (it->second <= 0)
          engine.flags.erase(it);
      }
    }

    /// Unlock a codex entry (`unlock_codex('id')`): set its `codex.<id>` flag, mark the codex
    /// cache stale, and notify only when the entry was not already unlocked.
    void handle_unlock_codex(Engine &engine, Gameplay &gameplay, const dialogue::EventAction &ev) {
      const std::string key{codex::flag_key(ev.args[0])};
      screens::codex_mark_dirty(gameplay.codex_screen);
      if (world::has_flag(engine.flags, key))
        return;
      world::set_flag(engine.flags, key);
      if (const codex::CodexEntry *entry = gameplay.codex.find(ev.args[0]); entry != nullptr)
        engine.notify(std::format("Codex updated: {}", entry->title), ui::k_toast_default_colour);
    }

    /// Discover a fast-travel location (`discover_location('id')`): set its discovery flag and
    /// notify only when it was not already known.
    void handle_discover_location(Engine &engine, const Gameplay &gameplay, const dialogue::EventAction &ev) {
      const std::string key{location::discovery_flag_key(ev.args[0])};
      if (world::has_flag(engine.flags, key))
        return;
      world::set_flag(engine.flags, key);
      if (const location::Location *location = gameplay.locations.find(ev.args[0]); location != nullptr)
        engine.notify(std::format("Location discovered: {}", location->name), ui::k_toast_default_colour);
    }

    /// Open a container's two-pane loot screen (`open_container('id')`). The screen is pushed onto
    /// the UI stack so closing it returns to whatever was beneath (dialogue included).
    void handle_open_container(Engine &engine, Gameplay &gameplay, const dialogue::EventAction &ev) {
      gameplay.active_container_id = ev.args[0];
      gameplay.loot_screen = {};
      engine.scene.ui.push(screens::Loot);
    }

    /// Open a merchant's barter screen (`open_shop('id')`). Unknown ids warn and are ignored.
    void handle_open_shop(Engine &engine, Gameplay &gameplay, const dialogue::EventAction &ev) {
      if (gameplay.shops.find(ev.args[0]) == nullptr) {
        warn_log("[gameplay] WARN: open_shop unknown shop '{}'", ev.args[0]);
        return;
      }
      gameplay.active_shop_id = ev.args[0];
      gameplay.barter_screen = {};
      engine.scene.ui.push(screens::Barter);
    }

    void dispatch_dialogue_event(Gameplay &gameplay, Engine &engine, quest::Runner &quest_runner,
                                 const dialogue::EventAction &ev) noexcept {
      try {
        if (ev.name == "play_sound" && !ev.args.empty()) {
          handle_play_sound(engine, ev);
        } else if (ev.name == "quest_start" && !ev.args.empty()) {
          handle_quest_start(engine, gameplay, quest_runner, ev);
        } else if (ev.name == "quest_advance" && ev.args.size() >= 2) {
          handle_quest_advance(engine, gameplay, quest_runner, ev);
        } else if (ev.name == "give_item" && !ev.args.empty()) {
          engine.flags[item_flag_key(ev.args[0])] += event_int_arg(ev, 1, /*fallback=*/1);
        } else if (ev.name == "take_item" && !ev.args.empty()) {
          handle_take_item(engine, ev);
        } else if (ev.name == "unlock_codex" && !ev.args.empty()) {
          handle_unlock_codex(engine, gameplay, ev);
        } else if (ev.name == "discover_location" && !ev.args.empty()) {
          handle_discover_location(engine, gameplay, ev);
        } else if (ev.name == "open_container" && !ev.args.empty()) {
          handle_open_container(engine, gameplay, ev);
        } else if (ev.name == "open_shop" && !ev.args.empty()) {
          handle_open_shop(engine, gameplay, ev);
        } else if (ev.name == "reputation" && ev.args.size() >= 2) {
          // A non-numeric value parses to 0; skip the write so no zero-valued rep flag is created.
          if (const int delta = event_int_arg(ev, 1, /*fallback=*/0); delta != 0)
            engine.flags["rep." + ev.args[0]] += delta;
        } else if (invoke_event_hook(gameplay, engine, ev) == EventHookResult::NotHandled) {
          warn_log("[gameplay] WARN: unknown dialogue event '{}'", ev.name);
        }
      } catch (...) {
        // Skip events whose processing throws (e.g. allocation failure); preserves the noexcept
        // contract of process_events.
        return;
      }
    }

    /// Modulo wrap of a UI list cursor, matching the dialogue choice cursor: Down past the last
    /// row lands on the first, Up past the first lands on the last. @p count must be > 0.
    int wrap_cursor(int current, int delta, int count) noexcept {
      return (current + delta + count) % count;
    }

    /// The window's size in logical points, the space panels and the cursor share.
    core::math::Vec2 screen_viewport(const Engine &engine) noexcept {
      const auto [width, height] = engine.window->size();
      return {.x = static_cast<float>(width), .y = static_cast<float>(height)};
    }

    /// Whether @p cursor lies inside the half-open screen-space @p rect.
    bool cursor_in(core::math::Vec2 cursor, const ui::RowRect &rect) noexcept {
      return cursor.x >= rect.pos.x && cursor.x <= rect.pos.x + rect.width && cursor.y >= rect.pos.y &&
             cursor.y <= rect.pos.y + rect.height;
    }

    /// The cursor's current position as a point.
    core::math::Vec2 intent_cursor(const input::InputIntent &intent) noexcept {
      return {.x = intent.cursor_x, .y = intent.cursor_y};
    }

    /// True when the step carries pointer or wheel motion a screen should react to.
    bool pointer_active(const input::InputIntent &intent) noexcept {
      return intent.mouse_moved || intent.cursor_clicked || intent.scroll_y != 0.f;
    }

    /// True when the cursor should re-focus the row it is over: a real move or a click.
    bool pointer_focus(const input::InputIntent &intent) noexcept {
      return intent.mouse_moved || intent.cursor_clicked;
    }

    /** @brief Ratio above which the dominant axis is considered "cardinal" rather than diagonal
     *  when computing facing direction. */
    constexpr float k_cardinal_dominance_ratio = 2.f;

    /// Classify a tile-grid displacement (dx=Δcol, dy=Δrow) into the nearest screen-space
    /// Direction. The isometric projection rotates the grid axes 45° relative to screen space,
    /// so tile-cardinal directions (pure dc/dr) map to screen-intercardinal and vice versa:
    ///   Tile SE (+,+) → screen South,  Tile NW (-,-) → screen North,
    ///   Tile NE (+,-) → screen East,   Tile SW (-,+) → screen West.
    [[nodiscard]] corundum::core::Direction dir_from_delta(float dx, float dy) noexcept {
      using corundum::core::Direction;
      const float ax = std::abs(dx);
      const float ay = std::abs(dy);
      if (ay > k_cardinal_dominance_ratio * ax)
        return dy > 0.f ? Direction::SouthWest : Direction::NorthEast;
      if (ax > k_cardinal_dominance_ratio * ay)
        return dx > 0.f ? Direction::SouthEast : Direction::NorthWest;
      if (dx > 0.f)
        return dy > 0.f ? Direction::South : Direction::East;
      return dy > 0.f ? Direction::West : Direction::North;
    }

    /// One row of the menu hub's action → tab table.
    struct HubTabAction {
      input::Action action{};

      world::GameMode mode{};
    };

    /// The keyboard hotkeys that open a hub tab directly (I/J/C/M).
    constexpr std::array<HubTabAction, 4> k_hub_tab_actions{
        {
            {.action = input::Action::Inventory, .mode = screens::Inventory},
            {.action = input::Action::Journal, .mode = screens::Journal},
            {.action = input::Action::Codex, .mode = screens::Codex},
            {.action = input::Action::Map, .mode = screens::Map},
        },
    };

    /// Next hub tab relative to @p mode by @p direction (+1 / -1), wrapping.
    world::GameMode cycle_hub_tab(world::GameMode mode, int direction) noexcept {
      std::size_t index = 0;
      for (std::size_t i = 0; i < screens::k_hub_tab_modes.size(); ++i) {
        if (screens::k_hub_tab_modes[i] == mode)
          index = i;
      }
      const auto count = static_cast<int>(screens::k_hub_tab_modes.size());
      const int next = (((static_cast<int>(index) + direction) % count) + count) % count;
      return screens::k_hub_tab_modes[static_cast<std::size_t>(next)];
    }

    /// Reset a hub tab's open-time state: its cursor, and any cache the tab owns.
    void open_hub_tab(const Engine &engine, Gameplay &gameplay, world::GameMode mode) {
      gameplay.last_hub_mode = mode;
      switch (mode) {
        case screens::Inventory:
          gameplay.inventory_cursor = 0;
          // Built once here rather than every render frame: the inventory is read-only and the
          // simulation is paused while it is open, so there is no mutation to invalidate it.
          gameplay.inventory_lines = screens::build_inventory_lines(engine.flags, gameplay.items);
          break;
        case screens::Journal:
          gameplay.journal_screen.cursor = 0;
          break;
        case screens::Codex:
          gameplay.codex_screen.cursor = 0;
          gameplay.codex_screen.scroll = 0.f;
          screens::codex_mark_dirty(gameplay.codex_screen);
          screens::refresh_codex(gameplay.codex_screen, gameplay.codex, engine.flags);
          break;
        case screens::Map:
          gameplay.map_screen.cursor = 0;
          break;
        default:
          break;
      }
    }

    /// Replace the top hub layer with @p mode (tab switch), reselecting that tab's state.
    void switch_hub_tab(Engine &engine, Gameplay &gameplay, world::GameMode mode) {
      engine.scene.ui.pop();
      engine.scene.ui.push(mode);
      open_hub_tab(engine, gameplay, mode);
    }

    /// Handle a click or wheel over the hub tab strip. Returns true when the step was consumed.
    bool handle_hub_strip_pointer(Engine &engine, Gameplay &gameplay, const input::InputIntent &intent) {
      if (!pointer_active(intent))
        return false;

      const screens::HubTabStrip strip =
          screens::hub_tab_strip(*engine.renderer, engine.render.panel_skin.style, screen_viewport(engine));
      const int tab = screens::hub_tab_at(strip, intent_cursor(intent));
      if (tab < 0)
        return false;

      const world::GameMode mode = screens::k_hub_tab_modes[static_cast<std::size_t>(tab)];
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

    /// The gameplay framework's pre-dispatch input hook: handles the menu hub's open/close/switch
    /// and tab cycling. Returns true when it consumed the step; safe to register as the only
    /// entry in Engine::screen_input_hooks.
    bool handle_hub_input(Engine &engine, Gameplay &gameplay, const input::InputIntent &intent) {
      using world::GameMode;

      // The menu hub: one gamepad button (Hub) toggles the last-used tab, and the I/J/C/M hotkeys
      // select a tab directly. The hub is not a GameMode of its own — it is the convention that the
      // top of the UI stack is one of Inventory/Journal/Codex/Map. Opening, switching or closing
      // consumes the step so the same press cannot also act on the fresh screen.
      const bool on_hub_tab = screens::is_hub_mode(engine.scene.mode());
      if (engine.input_state.is_pressed(input::Action::Hub)) {
        if (on_hub_tab) {
          engine.scene.ui.pop();
          return true;
        }
        if (engine.scene.mode() == GameMode::Exploring) {
          open_hub_tab(engine, gameplay, gameplay.last_hub_mode);
          engine.scene.ui.push(gameplay.last_hub_mode);
          return true;
        }
      }
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
        if (engine.scene.mode() == GameMode::Exploring) {
          open_hub_tab(engine, gameplay, row.mode);
          engine.scene.ui.push(row.mode);
          return true;
        }
        // Another screen (dialogue, prompt, menu, loot, barter) is on top: the hotkey does nothing.
      }

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

    /// Step the Inventory hub tab: Cancel closes it; a hovered row takes focus on a real mouse
    /// move or click; the wheel moves one row per notch; Up/Down wrap the highlight.
    void update_inventory(Engine &engine, Gameplay &gameplay, const input::InputIntent &intent) {
      if (intent.back) {
        engine.scene.ui.pop();
        return;
      }
      const int rows = static_cast<int>(gameplay.inventory_lines.size());

      if (pointer_active(intent)) {
        const screens::InventoryLayout layout =
            screens::inventory_panel_layout(*engine.renderer, engine.render.panel_skin.style, gameplay.inventory_lines,
                                            gameplay.inventory_cursor, screen_viewport(engine));
        const core::math::Vec2 cursor = intent_cursor(intent);
        if (pointer_focus(intent)) {
          if (const int hovered = ui::hovered_row(cursor, layout.rows); hovered >= 0)
            gameplay.inventory_cursor = hovered;
        }
        if (const int steps = ui::scroll_row_delta(intent.scroll_y); steps != 0 && rows > 0)
          gameplay.inventory_cursor = wrap_cursor(gameplay.inventory_cursor, steps, rows);
      }

      const int delta = intent.navigate_y;
      if (delta == 0)
        return;
      if (rows <= 0) {
        gameplay.inventory_cursor = 0;
        return;
      }
      gameplay.inventory_cursor = wrap_cursor(gameplay.inventory_cursor, delta, rows);
    }

    /// Switch the journal to @p tab, resetting the row cursor when it changes.
    void select_journal_tab(screens::JournalState &state, screens::JournalTab tab) noexcept {
      if (state.tab == tab)
        return;
      state.tab = tab;
      state.cursor = 0;
    }

    /// Toggle tracking on @p entry: tracking it clears the flag on every other started quest.
    /// Only an Active quest is trackable.
    void toggle_journal_track(Engine &engine, const Gameplay &gameplay, const screens::JournalEntry &entry) {
      if (entry.lifecycle != quest::Lifecycle::Active)
        return;
      const std::string key = quest::tracked_flag_key(entry.id);
      if (world::has_flag(engine.flags, key)) {
        world::clear_flag(engine.flags, key);
        return;
      }
      for (const quest::Quest *started : quest::started_quests(gameplay.quests, engine.flags)) {
        if (started != nullptr)
          world::clear_flag(engine.flags, quest::tracked_flag_key(started->quest_id));
      }
      world::set_flag(engine.flags, key);
    }

    /// Apply mouse intent to the journal: a click on the sub-tab strip selects a tab, the wheel
    /// over it cycles sub-tabs, a hovered row takes focus on a real mouse move or click, and the
    /// wheel moves one row per notch. Returns true when the pointer switched tabs and the step
    /// should be consumed without also activating a row.
    bool apply_journal_pointer(const Engine &engine, Gameplay &gameplay, const input::InputIntent &intent,
                               const std::vector<screens::JournalEntry> &entries) {
      if (!pointer_active(intent))
        return false;

      screens::JournalState &state = gameplay.journal_screen;
      const screens::JournalLayout layout =
          screens::journal_panel_layout(*engine.renderer, engine.render.panel_skin.style, entries, state,
                                        screen_viewport(engine), engine.input_mapper.last_device());
      const core::math::Vec2 cursor = intent_cursor(intent);
      if (const int sub = ui::hovered_row(cursor, layout.sub_tabs); sub >= 0) {
        if (intent.cursor_clicked) {
          select_journal_tab(state, screens::k_journal_tabs[static_cast<std::size_t>(sub)]);
          return true;
        }
        if (intent.scroll_y != 0.f) {
          select_journal_tab(state, screens::cycle_journal_tab(state.tab, intent.scroll_y > 0.f ? -1 : 1));
          return true;
        }
      }
      if (pointer_focus(intent)) {
        if (const int hovered = ui::hovered_row(cursor, layout.rows); hovered >= 0)
          state.cursor = hovered;
      }
      const int rows = static_cast<int>(entries.size());
      if (const int steps = ui::scroll_row_delta(intent.scroll_y); steps != 0 && rows > 0)
        state.cursor = wrap_cursor(state.cursor, steps, rows);
      return false;
    }

    /// Step the Journal hub tab: Cancel closes it; SubTabNext/Prev cycle sub-tabs (resetting the
    /// cursor); a click on the sub-tab strip selects a tab and the wheel over it cycles; a hovered
    /// row takes focus on a real mouse move or click; the wheel moves one row per notch; Activate
    /// toggles tracking on the highlighted Active quest; Up/Down wrap the highlight.
    void update_journal(Engine &engine, Gameplay &gameplay, const input::InputIntent &intent) {
      screens::JournalState &state = gameplay.journal_screen;
      if (intent.back) {
        engine.scene.ui.pop();
        return;
      }

      if (intent.next_sub_tab || intent.prev_sub_tab)
        select_journal_tab(state, screens::cycle_journal_tab(state.tab, intent.next_sub_tab ? 1 : -1));

      const std::vector<screens::JournalEntry> entries =
          screens::build_journal_entries(gameplay.quests, engine.flags, state.tab, engine.scene.zone_id);
      if (apply_journal_pointer(engine, gameplay, intent, entries))
        return;

      const int rows = static_cast<int>(entries.size());
      if (intent.activate && rows > 0) {
        const std::size_t index = static_cast<std::size_t>(std::clamp(state.cursor, 0, rows - 1));
        toggle_journal_track(engine, gameplay, entries[index]);
      }

      const int delta = intent.navigate_y;
      if (delta == 0)
        return;
      if (rows <= 0) {
        state.cursor = 0;
        return;
      }
      state.cursor = wrap_cursor(state.cursor, delta, rows);
    }

    /// Step the Codex hub tab: Cancel closes it, Up/Down move the highlighted entry, the wheel
    /// over the list moves the cursor and over the detail body scrolls it, and a hovered list row
    /// takes focus on a real mouse move or click. Rows are refreshed from the registry + flags on
    /// the first step after an open or unlock (dirty-flagged). The Codex hotkey is handled by the
    /// hub table, which toggles this tab off before this step would run.
    void update_codex(Engine &engine, Gameplay &gameplay, const input::InputIntent &intent) {
      screens::CodexState &state = gameplay.codex_screen;
      screens::refresh_codex(state, gameplay.codex, engine.flags);

      if (intent.back) {
        engine.scene.ui.pop();
        return;
      }

      const int rows = static_cast<int>(state.entries.size());
      if (pointer_active(intent)) {
        const screens::CodexLayout layout =
            screens::codex_panel_layout(*engine.renderer, engine.render.panel_skin.style, state.entries, state.cursor,
                                        screen_viewport(engine), engine.input_mapper.last_device());
        const core::math::Vec2 cursor = intent_cursor(intent);
        if (pointer_focus(intent)) {
          for (const screens::CodexListRow &row : layout.list_rows) {
            if (row.entry_index >= 0 && cursor_in(cursor, row.rect)) {
              state.cursor = row.entry_index;
              state.scroll = 0.f;
              break;
            }
          }
        }
        if (intent.scroll_y != 0.f) {
          if (cursor_in(cursor, layout.body_rect)) {
            state.scroll = std::max(0.f, state.scroll - intent.scroll_y);
          } else if (const int steps = ui::scroll_row_delta(intent.scroll_y); steps != 0 && rows > 0) {
            state.cursor = wrap_cursor(state.cursor, steps, rows);
            state.scroll = 0.f;
          }
        }
      }

      if (intent.navigate_y != 0) {
        if (rows > 0)
          state.cursor = wrap_cursor(state.cursor, intent.navigate_y, rows);
        state.scroll = 0.f;
      }
    }

    /// Step the Map hub tab: Cancel closes it, a hovered row takes focus on a real mouse move or
    /// click, Up/Down and the wheel move the highlighted destination, and Activate fast-travels.
    /// A destination already in the active zone is a no-op.
    void update_map(Engine &engine, Gameplay &gameplay, const input::InputIntent &intent) {
      if (intent.back) {
        engine.scene.ui.pop();
        return;
      }

      const std::vector<screens::MapEntry> entries =
          screens::build_map_entries(gameplay.locations, engine.flags, engine.scene.zone_id);
      const int rows = static_cast<int>(entries.size());

      if (pointer_active(intent)) {
        const screens::MapLayout layout =
            screens::map_panel_layout(*engine.renderer, engine.render.panel_skin.style, entries,
                                      screen_viewport(engine), engine.input_mapper.last_device());
        const core::math::Vec2 cursor = intent_cursor(intent);
        if (pointer_focus(intent)) {
          if (const int hovered = ui::hovered_row(cursor, layout.rows); hovered >= 0)
            gameplay.map_screen.cursor = hovered;
        }
        if (const int steps = ui::scroll_row_delta(intent.scroll_y); steps != 0 && rows > 0)
          gameplay.map_screen.cursor = wrap_cursor(gameplay.map_screen.cursor, steps, rows);
      }

      if (intent.navigate_y != 0 && rows > 0)
        gameplay.map_screen.cursor = wrap_cursor(gameplay.map_screen.cursor, intent.navigate_y, rows);
      if (!intent.activate || rows == 0)
        return;

      const screens::MapEntry &selected =
          entries[static_cast<std::size_t>(std::clamp(gameplay.map_screen.cursor, 0, rows - 1))];
      if (selected.current) {
        engine.notify("You are already here");
        return;
      }

      const location::Location *location = gameplay.locations.find(selected.id);
      if (location == nullptr)
        return;
      world::MapTransition transition{
          .spawn_col = static_cast<int>(location->col),
          .spawn_row = static_cast<int>(location->row),
          .return_to_world = location->return_to_world || location->map.empty(),
      };
      if (!transition.return_to_world)
        transition.target_map = location->map;
      engine.scene.pending_transition = std::move(transition);
      engine.scene.ui.pop();
    }

    /// Add @p delta to @p key's count, creating it when positive and erasing it once it reaches
    /// zero. Keeps the FlagStore free of zero-valued rows (matching item grant/take semantics).
    void adjust_flag(world::FlagStore &flags, const std::string &key, int delta) {
      if (delta == 0)
        return;
      const auto it = flags.find(key);
      if (it == flags.end()) {
        if (delta > 0)
          flags.emplace(key, delta);
        return;
      }
      it->second += delta;
      if (it->second <= 0)
        flags.erase(it);
    }

    /// Apply mouse intent to the loot screen: hovering a pane switches to it, hovering a row (on a
    /// real move or click) focuses it, and the wheel moves one row per notch.
    void apply_loot_pointer(const Engine &engine, Gameplay &gameplay, const input::InputIntent &intent,
                            std::string_view container_name, const std::vector<screens::InventoryLine> &container_lines,
                            const std::vector<screens::InventoryLine> &player_lines) {
      if (!pointer_active(intent))
        return;

      const screens::LootLayout layout =
          screens::loot_panel_layout(*engine.renderer, engine.render.panel_skin.style, container_name, container_lines,
                                     player_lines, screen_viewport(engine), engine.input_mapper.last_device());
      const core::math::Vec2 cursor = intent_cursor(intent);

      if (pointer_focus(intent)) {
        const bool over_player = cursor_in(cursor, layout.player_pane);
        if (over_player || cursor_in(cursor, layout.container_pane)) {
          const screens::LootPane pane = over_player ? screens::LootPane::Player : screens::LootPane::Container;
          if (gameplay.loot_screen.pane != pane) {
            gameplay.loot_screen.pane = pane;
            gameplay.loot_screen.cursor = 0;
          }
          if (const int hovered = ui::hovered_row(cursor, over_player ? layout.player_rows : layout.container_rows);
              hovered >= 0)
            gameplay.loot_screen.cursor = hovered;
        }
      }

      const int active_rows = static_cast<int>(
          (gameplay.loot_screen.pane == screens::LootPane::Container ? container_lines : player_lines).size());
      if (const int steps = ui::scroll_row_delta(intent.scroll_y); steps != 0 && active_rows > 0)
        gameplay.loot_screen.cursor = wrap_cursor(gameplay.loot_screen.cursor, steps, active_rows);
    }

    /// Step the loot screen: Back closes, Left/Right or a click switch the active pane, a hovered
    /// row takes focus on a real mouse move or click, the wheel moves one row per notch, and
    /// Activate moves one unit of the highlighted item to the other holder.
    void update_loot(Engine &engine, Gameplay &gameplay, const input::InputIntent &intent) {
      if (intent.back) {
        gameplay.active_container_id.clear();
        engine.scene.ui.pop();
        return;
      }

      const std::string container_name =
          gameplay.active_container_id.empty() ? std::string{"Container"} : gameplay.active_container_id;
      const std::vector<screens::InventoryLine> container_lines = screens::build_item_lines(
          engine.flags, gameplay.items, item::container_flag_prefix(gameplay.active_container_id));
      const std::vector<screens::InventoryLine> player_lines =
          screens::build_item_lines(engine.flags, gameplay.items, item::k_flag_prefix);

      if (intent.navigate_x != 0) {
        gameplay.loot_screen.pane = gameplay.loot_screen.pane == screens::LootPane::Container
                                        ? screens::LootPane::Player
                                        : screens::LootPane::Container;
        gameplay.loot_screen.cursor = 0;
      }

      apply_loot_pointer(engine, gameplay, intent, container_name, container_lines, player_lines);

      const bool container_active = gameplay.loot_screen.pane == screens::LootPane::Container;
      const std::vector<screens::InventoryLine> &lines = container_active ? container_lines : player_lines;
      const int rows = static_cast<int>(lines.size());

      if (intent.navigate_y != 0 && rows > 0)
        gameplay.loot_screen.cursor = wrap_cursor(gameplay.loot_screen.cursor, intent.navigate_y, rows);
      if (!intent.activate || rows == 0)
        return;

      const screens::InventoryLine &selected =
          lines[static_cast<std::size_t>(std::clamp(gameplay.loot_screen.cursor, 0, rows - 1))];
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

    /// Flip the barter Buy/Sell tab and reset its row cursor.
    void toggle_barter_tab(Gameplay &gameplay) noexcept {
      gameplay.barter_screen.tab =
          gameplay.barter_screen.tab == screens::BarterTab::Buy ? screens::BarterTab::Sell : screens::BarterTab::Buy;
      gameplay.barter_screen.cursor = 0;
    }

    /// Apply mouse intent to the barter screen. Returns true when the pointer switched tabs and
    /// the step should be consumed without also trading.
    bool apply_barter_pointer(const Engine &engine, Gameplay &gameplay, const input::InputIntent &intent,
                              const shop::Shop &shop, const std::vector<screens::BarterLine> &lines) {
      if (!pointer_active(intent))
        return false;

      const screens::BarterLayout layout = screens::barter_panel_layout(
          *engine.renderer, engine.render.panel_skin.style, shop.name,
          world::visit_count(engine.flags, std::string{screens::k_gold_flag}), lines, gameplay.barter_screen,
          screen_viewport(engine), engine.input_mapper.last_device());
      const core::math::Vec2 cursor = intent_cursor(intent);
      const bool over_tabs = cursor_in(cursor, layout.tabs[0]) || cursor_in(cursor, layout.tabs[1]);

      if (intent.cursor_clicked && over_tabs) {
        const screens::BarterTab clicked =
            cursor_in(cursor, layout.tabs[0]) ? screens::BarterTab::Buy : screens::BarterTab::Sell;
        if (clicked != gameplay.barter_screen.tab) {
          gameplay.barter_screen.tab = clicked;
          gameplay.barter_screen.cursor = 0;
        }
        return true;
      }
      if (intent.scroll_y != 0.f && over_tabs) {
        toggle_barter_tab(gameplay);
        return true;
      }

      if (pointer_focus(intent)) {
        if (const int hovered = ui::hovered_row(cursor, layout.rows); hovered >= 0)
          gameplay.barter_screen.cursor = hovered;
      }
      if (const int steps = ui::scroll_row_delta(intent.scroll_y); steps != 0 && !lines.empty())
        gameplay.barter_screen.cursor =
            wrap_cursor(gameplay.barter_screen.cursor, steps, static_cast<int>(lines.size()));
      return false;
    }

    /// Step the barter screen: Back closes, Left/Right, Tab or a click switch Buy/Sell, a hovered
    /// row takes focus on a real mouse move or click, the wheel moves one row per notch (or
    /// switches tabs over the tab strip), and Activate performs the trade.
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

      const int reputation =
          shop->faction.empty() ? 0 : world::visit_count(engine.flags, std::string{"rep."} + shop->faction);
      const std::vector<screens::BarterLine> lines =
          gameplay.barter_screen.tab == screens::BarterTab::Buy
              ? screens::build_barter_stock(*shop, gameplay.items, reputation)
              : screens::build_barter_sell_lines(*shop, gameplay.items, engine.flags);
      const int rows = static_cast<int>(lines.size());

      if (intent.next_tab || intent.prev_tab || intent.navigate_x != 0)
        toggle_barter_tab(gameplay);

      if (apply_barter_pointer(engine, gameplay, intent, *shop, lines))
        return;

      if (intent.navigate_y != 0 && rows > 0)
        gameplay.barter_screen.cursor = wrap_cursor(gameplay.barter_screen.cursor, intent.navigate_y, rows);
      if (!intent.activate || rows == 0)
        return;

      const screens::BarterLine &selected =
          lines[static_cast<std::size_t>(std::clamp(gameplay.barter_screen.cursor, 0, rows - 1))];
      int gold = world::visit_count(engine.flags, std::string{screens::k_gold_flag});
      if (gameplay.barter_screen.tab == screens::BarterTab::Buy) {
        if (selected.unit_price > gold) {
          engine.notify("Not enough gold");
          return;
        }
        gold -= selected.unit_price;
        adjust_flag(engine.flags, item_flag_key(selected.id), 1);
      } else {
        if (selected.unit_price <= 0 || selected.count <= 0)
          return;
        gold += selected.unit_price;
        adjust_flag(engine.flags, item_flag_key(selected.id), -1);
      }
      engine.flags[std::string{screens::k_gold_flag}] = gold;
    }

    /// Gameplay HUD strip: hidden while any modal is up. The engine gates the Hud layer on an
    /// empty UI stack and no transition prompt; the dialogue check is defensive.
    void render_hud(const Engine &engine, const Gameplay &gameplay, platform::Renderer &r,
                    core::math::Vec2 /*viewport*/) {
      if (gameplay.dialogue)
        return;
      screens::hud_strip_render(r, engine.render.panel_skin.style, engine.render.panel_skin.border,
                                screens::build_hud_strip(engine.flags, gameplay.quests, engine.scene.zone_id));
    }

    /// Modal dialogue box: draws only while a conversation is active and Dialogue is the top
    /// mode, so a screen pushed over it (the pause menu) never double-draws. Hiding clears
    /// visibility only — the cached layout and reveal progress are untouched, so popping the
    /// screen above restores the box exactly.
    void render_dialogue(const Engine &engine, Gameplay &gameplay, platform::Renderer &r, core::math::Vec2 viewport) {
      if (gameplay.dialogue && engine.scene.mode() == screens::Dialogue)
        screens::dialog_box_update(gameplay.dialog_box, *gameplay.dialogue, r, viewport, engine.render.panel_skin,
                                   engine.render.text_speed);
      else
        screens::dialog_box_hide(gameplay.dialog_box);
      screens::dialog_box_render(gameplay.dialog_box, r, engine.render.panel_skin);
    }

    void render_inventory(const Engine &engine, const Gameplay &gameplay, platform::Renderer &r,
                          core::math::Vec2 viewport) {
      screens::inventory_panel_render(r, engine.render.panel_skin.style, engine.render.panel_skin.border,
                                      gameplay.inventory_lines, gameplay.inventory_cursor, viewport);
    }

    void render_journal(const Engine &engine, const Gameplay &gameplay, platform::Renderer &r,
                        core::math::Vec2 viewport) {
      screens::journal_panel_render(r, engine.render.panel_skin.style, engine.render.panel_skin.border,
                                    screens::build_journal_entries(gameplay.quests, engine.flags,
                                                                   gameplay.journal_screen.tab, engine.scene.zone_id),
                                    gameplay.journal_screen, viewport, engine.input_mapper.last_device());
    }

    void render_codex(const Engine &engine, const Gameplay &gameplay, platform::Renderer &r,
                      core::math::Vec2 viewport) {
      if (engine.scene.mode() != screens::Codex)
        return;
      screens::codex_panel_render(r, engine.render.panel_skin.style, engine.render.panel_skin.border,
                                  gameplay.codex_screen.entries, gameplay.codex_screen.cursor,
                                  gameplay.codex_screen.scroll, viewport, engine.input_mapper.last_device());
    }

    void render_map(const Engine &engine, const Gameplay &gameplay, platform::Renderer &r, core::math::Vec2 viewport) {
      if (engine.scene.mode() != screens::Map)
        return;
      screens::map_panel_render(r, engine.render.panel_skin.style, engine.render.panel_skin.border,
                                screens::build_map_entries(gameplay.locations, engine.flags, engine.scene.zone_id),
                                gameplay.map_screen.cursor, viewport, engine.input_mapper.last_device());
    }

    void render_loot(const Engine &engine, const Gameplay &gameplay, platform::Renderer &r, core::math::Vec2 viewport) {
      if (engine.scene.mode() != screens::Loot)
        return;
      const std::string container_name =
          gameplay.active_container_id.empty() ? std::string{"Container"} : gameplay.active_container_id;
      screens::loot_panel_render(r, engine.render.panel_skin.style, engine.render.panel_skin.border, container_name,
                                 screens::build_item_lines(engine.flags, gameplay.items,
                                                           item::container_flag_prefix(gameplay.active_container_id)),
                                 screens::build_item_lines(engine.flags, gameplay.items, item::k_flag_prefix),
                                 gameplay.loot_screen, viewport, engine.input_mapper.last_device());
    }

    void render_barter(const Engine &engine, const Gameplay &gameplay, platform::Renderer &r,
                       core::math::Vec2 viewport) {
      if (engine.scene.mode() != screens::Barter)
        return;
      const shop::Shop *shop = gameplay.shops.find(gameplay.active_shop_id);
      if (shop == nullptr)
        return;
      const int reputation =
          shop->faction.empty() ? 0 : world::visit_count(engine.flags, std::string{"rep."} + shop->faction);
      const std::vector<screens::BarterLine> lines =
          gameplay.barter_screen.tab == screens::BarterTab::Buy
              ? screens::build_barter_stock(*shop, gameplay.items, reputation)
              : screens::build_barter_sell_lines(*shop, gameplay.items, engine.flags);
      screens::barter_panel_render(r, engine.render.panel_skin.style, engine.render.panel_skin.border, shop->name,
                                   world::visit_count(engine.flags, std::string{screens::k_gold_flag}), lines,
                                   gameplay.barter_screen, viewport, engine.input_mapper.last_device());
    }

    /// The hub tab strip draws over every hub panel, in the top margin so it never overlaps one.
    void render_hub_strip(const Engine &engine, platform::Renderer &r, core::math::Vec2 viewport) {
      if (!screens::is_hub_mode(engine.scene.mode()))
        return;
      screens::hub_tab_strip_render(r, engine.render.panel_skin.style, engine.scene.mode(), viewport,
                                    engine.input_mapper.last_device());
    }

    void load_content(Gameplay &gameplay, Engine &engine) {
      int dialogue_loaded{0};
      if (!engine.cfg.paths.dialogue_dir.empty())
        dialogue_loaded = gameplay.graphs.load_all(engine.cfg.paths.dialogue_dir);
      corundum::detail::info_log("[gameplay] Loaded {} dialogue graphs from '{}'", dialogue_loaded,
                                 engine.cfg.paths.dialogue_dir);

      int quest_loaded{0};
      if (!engine.cfg.paths.quests_dir.empty())
        quest_loaded = gameplay.quests.load_all(engine.cfg.paths.quests_dir);
      corundum::detail::info_log("[gameplay] Loaded {} quests from '{}'", quest_loaded, engine.cfg.paths.quests_dir);

      int item_loaded{0};
      if (!engine.cfg.paths.items_dir.empty())
        item_loaded = gameplay.items.load_all(engine.cfg.paths.items_dir);
      corundum::detail::info_log("[gameplay] Loaded {} items from '{}'", item_loaded, engine.cfg.paths.items_dir);

      int codex_loaded{0};
      if (!engine.cfg.paths.codex_dir.empty())
        codex_loaded = gameplay.codex.load_all(engine.cfg.paths.codex_dir);
      corundum::detail::info_log("[gameplay] Loaded {} codex entries from '{}'", codex_loaded,
                                 engine.cfg.paths.codex_dir);

      int location_loaded{0};
      if (!engine.cfg.paths.locations_dir.empty())
        location_loaded = gameplay.locations.load_all(engine.cfg.paths.locations_dir);
      corundum::detail::info_log("[gameplay] Loaded {} locations from '{}'", location_loaded,
                                 engine.cfg.paths.locations_dir);

      int shop_loaded{0};
      if (!engine.cfg.paths.shops_dir.empty())
        shop_loaded = gameplay.shops.load_all(engine.cfg.paths.shops_dir);
      corundum::detail::info_log("[gameplay] Loaded {} shops from '{}'", shop_loaded, engine.cfg.paths.shops_dir);

      validate_quest_references(gameplay.graphs, gameplay.quests, gameplay.items);
      for (const auto &[shop_id, shop] : gameplay.shops) {
        for (const shop::StockEntry &entry : shop.stock) {
          if (gameplay.items.find(entry.item) == nullptr)
            warn_log("[gameplay] WARN: shop '{}' stocks unknown item '{}'", shop_id, entry.item);
        }
      }

      // Seed GameConfig::starting_flags into the FlagStore, before the first frame, so a
      // project's authored starting state (gold, reputation, "intro_seen") is in place.
      for (const auto &[key, value] : engine.cfg.starting_flags)
        engine.flags[key] = value;
    }

    /// Register the gameplay framework with @p engine: its fixed-step system, hub input hook,
    /// screens and render layers. Every closure captures `gameplay`, which outlives the engine
    /// under the Runtime member-order contract.
    void register_gameplay(Gameplay &gameplay, Engine &engine) {
      using world::GameMode;

      engine.screens.add(screens::Dialogue, ScreenSpec{.owns_step = false});
      engine.screens.add(
          screens::Inventory,
          ScreenSpec{
              .owns_step = true,
              .update = [&gameplay](Engine &e,
                                    const input::InputIntent &intent) { update_inventory(e, gameplay, intent); },
              .render = [&gameplay](Engine &e, platform::Renderer &r,
                                    core::math::Vec2 viewport) { render_inventory(e, gameplay, r, viewport); },
          });
      engine.screens.add(
          screens::Journal,
          ScreenSpec{
              .owns_step = true,
              .update = [&gameplay](Engine &e,
                                    const input::InputIntent &intent) { update_journal(e, gameplay, intent); },
              .render = [&gameplay](Engine &e, platform::Renderer &r,
                                    core::math::Vec2 viewport) { render_journal(e, gameplay, r, viewport); },
          });
      engine.screens.add(
          screens::Codex,
          ScreenSpec{
              .owns_step = true,
              .update = [&gameplay](Engine &e, const input::InputIntent &intent) { update_codex(e, gameplay, intent); },
          });
      engine.screens.add(
          screens::Map,
          ScreenSpec{
              .owns_step = true,
              .update = [&gameplay](Engine &e, const input::InputIntent &intent) { update_map(e, gameplay, intent); },
          });
      engine.screens.add(
          screens::Loot,
          ScreenSpec{
              .owns_step = true,
              .update = [&gameplay](Engine &e, const input::InputIntent &intent) { update_loot(e, gameplay, intent); },
          });
      engine.screens.add(screens::Barter, ScreenSpec{
                                              .owns_step = true,
                                              .update =
                                                  [&gameplay](Engine &e, const input::InputIntent &intent) {
                                                    update_barter(e, gameplay, intent);
                                                  },
                                          });

      engine.fixed_step_systems.emplace_back([&gameplay](Engine &, float dt) { gameplay.fixed_step(dt); });

      engine.screen_input_hooks.emplace_back(
          [&gameplay](Engine &e, const input::InputIntent &intent) { return handle_hub_input(e, gameplay, intent); });

      engine.screens.add_layer(RenderLayer::Hud, [&gameplay](Engine &e, platform::Renderer &r, core::math::Vec2 v) {
        render_hud(e, gameplay, r, v);
      });
      engine.screens.add_layer(RenderLayer::Modal, [&gameplay](Engine &e, platform::Renderer &r, core::math::Vec2 v) {
        render_dialogue(e, gameplay, r, v);
      });
      engine.screens.add_layer(RenderLayer::HubPanel,
                               [&gameplay](Engine &e, platform::Renderer &r, core::math::Vec2 v) {
                                 render_codex(e, gameplay, r, v);
                                 render_map(e, gameplay, r, v);
                                 render_loot(e, gameplay, r, v);
                                 render_barter(e, gameplay, r, v);
                               });
      engine.screens.add_layer(RenderLayer::HubStrip,
                               [](Engine &e, platform::Renderer &r, core::math::Vec2 v) { render_hub_strip(e, r, v); });
    }

  } // namespace

  Gameplay::Gameplay(Engine &engine) : engine_(&engine) {
    load_content(*this, engine);
    register_gameplay(*this, engine);
  }

  Gameplay::~Gameplay() = default;

  void Gameplay::process_events() noexcept {
    quest::Runner quest_runner{quests, engine_->flags};
    for (const auto &ev : pending_dialogue_events)
      dispatch_dialogue_event(*this, *engine_, quest_runner, ev);
    pending_dialogue_events.clear();
  }

  void Gameplay::fixed_step(float dt) {
    Engine &engine = *engine_;
    const input::InputIntent intent = input::make_input_intent(engine.input_state, engine.input_mapper.last_device());

    // Quick-save/quick-load (F5/F9) are the framework's dev shortcuts; handle them on a step the
    // world owns, so a step-owning screen cannot trigger a save mid-menu.
    if (engine.input_state.is_pressed(input::Action::QuickSave))
      std::ignore = quick_save();
    if (engine.input_state.is_pressed(input::Action::QuickLoad))
      std::ignore = quick_load();

    playtime_seconds += static_cast<double>(dt);

    // Interaction is a one-frame pulse: read and clear it unconditionally so an unconsumed value
    // (no target in range, wrong mode) never persists into the next step.
    const std::optional<corundum::entities::EntityId> interaction = engine.scene.pending_interaction;
    engine.scene.pending_interaction.reset();

    if (engine.scene.mode() == screens::Dialogue) {
      update_dialogue(intent);
    } else if (engine.scene.mode() == world::GameMode::Exploring && interaction) {
      try_interact(*interaction);
    }

    process_events();

    if (dialogue)
      screens::dialog_box_advance(dialog_box, *dialogue, dt, engine.render.text_speed);

    quest::tick_quests(quests, engine.flags, engine.scene.zone_id);
  }

  std::string Gameplay::current_location_name() const {
    if (const location::Location *loc = locations.find(engine_->scene.zone_id); loc != nullptr && !loc->name.empty())
      return loc->name;
    return engine_->scene.zone_id;
  }

  std::expected<void, std::string> Gameplay::save_to_slot(std::string_view slot_id) {
    const std::expected<std::filesystem::path, std::string> directory = save::saves_directory(engine_->cfg);
    if (!directory)
      return std::unexpected(directory.error());

    const auto result = save::save_game(*engine_, save::slot_path(*directory, slot_id), current_location_name(),
                                        static_cast<std::int64_t>(playtime_seconds));
    if (result)
      last_slot = std::string{slot_id};
    return result;
  }

  std::expected<void, std::string> Gameplay::load_from_slot(std::string_view slot_id) {
    const std::expected<std::filesystem::path, std::string> directory = save::saves_directory(engine_->cfg);
    if (!directory)
      return std::unexpected(directory.error());

    const auto result = save::load_game(*engine_, save::slot_path(*directory, slot_id));
    if (result)
      last_slot = std::string{slot_id};
    return result;
  }

  std::expected<void, std::string> Gameplay::quick_save() {
    std::expected<void, std::string> result = save_to_slot(save::k_quicksave_slot);
    if (result)
      engine_->notify("Game saved");
    else
      engine_->notify(std::format("Save failed: {}", result.error()), ui::k_toast_failed_colour);
    return result;
  }

  std::expected<void, std::string> Gameplay::quick_load() {
    std::expected<void, std::string> result = load_from_slot(save::k_quicksave_slot);
    if (result)
      engine_->notify("Game loaded");
    else
      engine_->notify(std::format("Load failed: {}", result.error()), ui::k_toast_failed_colour);
    return result;
  }

  std::expected<void, std::string> Gameplay::autosave() {
    return save_to_slot(save::k_autosave_slot);
  }

  void Gameplay::update_dialogue(const input::InputIntent &intent) {
    if (!dialogue)
      return;

    pending_dialogue_events = dialogue->update(intent);
    if (dialogue->is_active())
      return;

    if (dialogue_npc) {
      corundum::entities::World &world = engine_->scene.world;
      const dialogue::DialogueNpc &npc = *dialogue_npc;
      if (npc.saved_facing && world.facings.has(npc.entity))
        world.facings.dir_ref(npc.entity) = *npc.saved_facing;
      if (npc.saved_anim && world.sprites.has(npc.entity)) {
        world.sprites.anim_id_ref(npc.entity) = *npc.saved_anim;
        world.sprites.frame_index_ref(npc.entity) = 0;
      }
    }
    dialogue_npc.reset();
    dialogue.reset();
    engine_->scene.ui.pop();
  }

  void Gameplay::try_interact(corundum::entities::EntityId target) {
    using corundum::core::Direction;
    using corundum::sprites::AnimId;
    using corundum::sprites::to_anim;

    corundum::world::Scene &scene = engine_->scene;
    corundum::entities::World &world = scene.world;
    if (!corundum::world::player_present(scene))
      return;
    if (!world.dialogue_refs.has(target) || !world.transforms.has(target))
      return;

    const dialogue::Graph *const graph = graphs.find(world.dialogue_refs.get_graph_id(target));
    if (graph == nullptr)
      return;

    const std::uint32_t player_slot = world.transforms.dense_index(scene.player);
    const float player_col = world.transforms.col[player_slot];
    const float player_row = world.transforms.row[player_slot];
    const std::uint32_t npc_slot = world.transforms.dense_index(target);
    const float npc_col = world.transforms.col[npc_slot];
    const float npc_row = world.transforms.row[npc_slot];

    const Direction toward_npc = dir_from_delta(npc_col - player_col, npc_row - player_row);

    dialogue::DialogueNpc npc{.entity = target};
    if (world.facings.has(target)) {
      npc.saved_facing = world.facings.dir_of(target);
      const Direction face_player = corundum::core::opposite(toward_npc);
      world.facings.dir_ref(target) = face_player;
      if (world.sprites.has(target) && world.animations.has(target)) {
        npc.saved_anim = world.sprites.anim_id_ref(target);
        const AnimId dir_anim = to_anim(face_player);
        const bool has_dir_anim = world.animations.frame_count(target, dir_anim) > 0;
        world.sprites.anim_id_ref(target) = has_dir_anim ? dir_anim : AnimId::Default;
        world.sprites.frame_index_ref(target) = 0;
      }
    }

    dialogue_npc = npc;
    dialogue.emplace(*graph, engine_->flags, &quests, &graphs, scene.zone_id);
    scene.ui.push(screens::Dialogue);
    // Defensive: a click that both queued a path AND was close enough to trigger interact (same
    // frame) would otherwise leave that path to silently resume once the conversation ends,
    // walking the player toward wherever they clicked to start it.
    scene.path.clear();
  }

} // namespace corundum::gameplay
