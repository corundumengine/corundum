// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include "gameplay/dialogue_events.hpp"

#include <corundum/gameplay/codex/codex.hpp>
#include <corundum/gameplay/faction/standing.hpp>
#include <corundum/gameplay/gameplay.hpp>
#include <corundum/gameplay/location/location.hpp>
#include <corundum/gameplay/quest/quest.hpp>
#include <corundum/gameplay/quest/runner.hpp>
#include <corundum/gameplay/quest/status.hpp>
#include <corundum/gameplay/quest/system.hpp>
#include <corundum/gameplay/screens/codex.hpp>
#include <corundum/gameplay/screens/modes.hpp>
#include <corundum/ui/toast.hpp>
#include <corundum/world/ui_stack.hpp>

#include "core/warn_log.hpp"
#include "gameplay/item_counts.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace corundum::gameplay {

namespace {

using detail::warn_log;

/** Parse event.args[index] as an int; returns `fallback` if absent, unparseable, or only
 *  partially numeric.
 */
int event_int_arg(const dialogue::EventAction &event, std::size_t index, int fallback) noexcept {
  if (index >= event.args.size())
    return fallback;
  const std::string &text = event.args[index];
  int value{fallback};
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size())
    return fallback;
  return value;
}

/** Outcome of consulting the game-provided dialogue-event hook. */
enum class EventHookResult : std::uint8_t {
  /** No hook is installed, or it reported the event as unhandled. */
  NotHandled,

  /** The hook reported the event as handled. */
  Handled,

  /** The hook threw; already logged, and the event is left suppressed. */
  Threw,
};

/** Invoke the game's dialogue-event hook, swallowing any exceptions so the noexcept contract
 *  on the dispatch loop holds. A throwing hook maps to Threw so dispatch does not also report
 *  the event as unknown.
 */
EventHookResult invoke_event_hook(const Gameplay &gameplay, Engine &engine,
                                  const dialogue::EventAction &event) noexcept {
  if (!gameplay.on_event)
    return EventHookResult::NotHandled;
  try {
    return gameplay.on_event(engine, event) ? EventHookResult::Handled : EventHookResult::NotHandled;
  } catch (...) {
    warn_log("[gameplay] WARN: on_event handler threw on '{}'", event.name);
    return EventHookResult::Threw;
  }
}

void handle_play_sound(Engine &engine, const dialogue::EventAction &event) {
  const auto result = engine.audio.play_sound(event.args[0]);
  if (!result)
    warn_log("[gameplay] WARN: {}", result.error());
}

void handle_quest_start(Engine &engine, const Gameplay &gameplay, quest::Runner &quest_runner,
                        const dialogue::EventAction &event) {
  const bool already_started = quest::get_stage(event.args[0], engine.flags) > 0;
  if (auto result = quest_runner.start(event.args[0]); !result) {
    warn_log("[gameplay] WARN: {}", result.error());
    return;
  }
  if (already_started)
    return;
  const quest::Quest *quest = gameplay.quests.find(event.args[0]);
  if (quest != nullptr)
    engine.notify(std::format("Quest started: {}", quest->name), ui::k_toast_default_colour);
  // Auto-track the newly started quest only when nothing else is tracked, so the HUD has a
  // default without overriding a quest the player pinned by hand.
  const bool any_tracked =
      std::ranges::any_of(quest::started_quests(gameplay.quests, engine.flags), [&engine](const quest::Quest *started) {
        return started != nullptr && world::has_flag(engine.flags, quest::tracked_flag_key(started->quest_id));
      });
  if (!any_tracked)
    world::set_flag(engine.flags, quest::tracked_flag_key(event.args[0]));
}

void handle_quest_advance(Engine &engine, const Gameplay &gameplay, quest::Runner &quest_runner,
                          const dialogue::EventAction &event) {
  const int stage_before = quest::get_stage(event.args[0], engine.flags);
  if (auto result = quest_runner.advance(event.args[0], event.args[1]); !result) {
    warn_log("[gameplay] WARN: {}", result.error());
    return;
  }
  // advance() reports ok even when the stage name was unknown (a logged no-op), so only
  // notify when the stage integer actually moved.
  if (quest::get_stage(event.args[0], engine.flags) == stage_before)
    return;
  const quest::Quest *quest = gameplay.quests.find(event.args[0]);
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

void handle_take_item(Engine &engine, const dialogue::EventAction &event) {
  const std::string key{item_flag_key(event.args[0])};
  if (const auto it = engine.flags.find(key); it != engine.flags.end()) {
    it->second -= event_int_arg(event, 1, /*fallback=*/1);
    if (it->second <= 0)
      engine.flags.erase(it);
  }
}

/** Tell the log and the player why a fight didn't start. */
void refuse_fight(Engine &engine, std::string_view reason) {
  warn_log("[gameplay] WARN: {}", reason);
  engine.notify(std::format("The fight can't start: {}", reason), ui::k_toast_failed_colour);
}

/** Queue a `start_encounter(actor, creature, ...)` fight, one pair per hostile; fixed_step()
 *  starts it once the mode is back to Exploring, after the dialogue that asked for it closes.
 */
void handle_start_encounter(Engine &engine, Gameplay &gameplay, const dialogue::EventAction &event) {
  if (gameplay.battle || gameplay.pending_encounter) {
    warn_log("[gameplay] WARN: start_encounter('{}') ignored: a fight is already queued or under way", event.args[0]);
    return;
  }
  if (event.args.size() % 2 != 0) {
    refuse_fight(engine, std::format("[gameplay] start_encounter takes actor and creature pairs, not {} arguments",
                                     event.args.size()));
    return;
  }
  combat::EncounterRequest request;
  for (std::size_t i = 0; i < event.args.size(); i += 2)
    request.hostiles.push_back({.actor_id = event.args[i], .creature_id = event.args[i + 1]});
  gameplay.pending_encounter = std::move(request);
}

/** Add a `reputation(faction, delta)` event's delta to the faction's standing. A non-numeric
 *  delta parses to 0 and is skipped so no zero-valued standing flag is created.
 */
void handle_reputation(Engine &engine, const dialogue::EventAction &event) {
  const int delta = event_int_arg(event, 1, /*fallback=*/0);
  if (delta == 0)
    return;
  const std::string key = faction::standing_flag_key(event.args[0]);
  engine.flags[key] = world::visit_count(engine.flags, key) + delta;
}

/** Unlock a codex entry (`unlock_codex('id')`): set its `codex.<id>` flag, mark the codex
 *  cache stale, and notify only when the entry was not already unlocked.
 */
void handle_unlock_codex(Engine &engine, Gameplay &gameplay, const dialogue::EventAction &event) {
  const std::string key{codex::flag_key(event.args[0])};
  screens::codex_mark_dirty(gameplay.codex_screen);
  if (world::has_flag(engine.flags, key))
    return;
  world::set_flag(engine.flags, key);
  if (const codex::CodexEntry *entry = gameplay.codex.find(event.args[0]); entry != nullptr)
    engine.notify(std::format("Codex updated: {}", entry->title), ui::k_toast_default_colour);
}

/** Discover a fast-travel location (`discover_location('id')`): set its discovery flag and
 *  notify only when it was not already known.
 */
void handle_discover_location(Engine &engine, const Gameplay &gameplay, const dialogue::EventAction &event) {
  const std::string key{location::discovery_flag_key(event.args[0])};
  if (world::has_flag(engine.flags, key))
    return;
  world::set_flag(engine.flags, key);
  if (const location::Location *location = gameplay.locations.find(event.args[0]); location != nullptr)
    engine.notify(std::format("Location discovered: {}", location->name), ui::k_toast_default_colour);
}

/** Open a container's two-pane loot screen (`open_container('id')`). The screen is pushed onto
 *  the UI stack so closing it returns to whatever was beneath (dialogue included).
 */
void handle_open_container(Engine &engine, Gameplay &gameplay, const dialogue::EventAction &event) {
  gameplay.active_container_id = event.args[0];
  gameplay.loot_screen = {};
  engine.scene.ui.push(screens::Loot);
}

/** Open a merchant's barter screen (`open_shop('id')`). Unknown ids warn and are ignored. */
void handle_open_shop(Engine &engine, Gameplay &gameplay, const dialogue::EventAction &event) {
  if (gameplay.shops.find(event.args[0]) == nullptr) {
    warn_log("[gameplay] WARN: open_shop unknown shop '{}'", event.args[0]);
    return;
  }
  gameplay.active_shop_id = event.args[0];
  gameplay.barter_screen = {};
  engine.scene.ui.push(screens::Barter);
}

/** Add the event's item count to its `item.<id>` flag (`give_item('id', n)`). */
void handle_give_item(Engine &engine, const dialogue::EventAction &event) {
  engine.flags[item_flag_key(event.args[0])] += event_int_arg(event, 1, /*fallback=*/1);
}

/** One row of the built-in dialogue-event table. */
struct BuiltinEvent {
  std::string_view name{};

  std::size_t min_args{};

  void (*handle)(Engine &, Gameplay &, quest::Runner &, const dialogue::EventAction &){};
};

/** The built-in dialogue events, in dispatch order. */
constexpr auto k_builtin_events = std::to_array<BuiltinEvent>({
    {
        .name = "play_sound",
        .min_args = 1,
        .handle = [](Engine &e, Gameplay &, quest::Runner &,
                     const dialogue::EventAction &event) { handle_play_sound(e, event); },
    },
    {
        .name = "quest_start",
        .min_args = 1,
        .handle = [](Engine &e, Gameplay &g, quest::Runner &q,
                     const dialogue::EventAction &event) { handle_quest_start(e, g, q, event); },
    },
    {
        .name = "quest_advance",
        .min_args = 2,
        .handle = [](Engine &e, Gameplay &g, quest::Runner &q,
                     const dialogue::EventAction &event) { handle_quest_advance(e, g, q, event); },
    },
    {
        .name = "give_item",
        .min_args = 1,
        .handle = [](Engine &e, Gameplay &, quest::Runner &,
                     const dialogue::EventAction &event) { handle_give_item(e, event); },
    },
    {
        .name = "take_item",
        .min_args = 1,
        .handle = [](Engine &e, Gameplay &, quest::Runner &,
                     const dialogue::EventAction &event) { handle_take_item(e, event); },
    },
    {
        .name = "unlock_codex",
        .min_args = 1,
        .handle = [](Engine &e, Gameplay &g, quest::Runner &,
                     const dialogue::EventAction &event) { handle_unlock_codex(e, g, event); },
    },
    {
        .name = "discover_location",
        .min_args = 1,
        .handle = [](Engine &e, Gameplay &g, quest::Runner &,
                     const dialogue::EventAction &event) { handle_discover_location(e, g, event); },
    },
    {
        .name = "open_container",
        .min_args = 1,
        .handle = [](Engine &e, Gameplay &g, quest::Runner &,
                     const dialogue::EventAction &event) { handle_open_container(e, g, event); },
    },
    {
        .name = "open_shop",
        .min_args = 1,
        .handle = [](Engine &e, Gameplay &g, quest::Runner &,
                     const dialogue::EventAction &event) { handle_open_shop(e, g, event); },
    },
    {
        .name = "reputation",
        .min_args = 2,
        .handle = [](Engine &e, Gameplay &, quest::Runner &,
                     const dialogue::EventAction &event) { handle_reputation(e, event); },
    },
    {
        .name = "start_encounter",
        .min_args = 2,
        .handle = [](Engine &e, Gameplay &g, quest::Runner &,
                     const dialogue::EventAction &event) { handle_start_encounter(e, g, event); },
    },
});

} // namespace

void dispatch_dialogue_event(Gameplay &gameplay, Engine &engine, quest::Runner &quest_runner,
                             const dialogue::EventAction &event) noexcept {
  try {
    const BuiltinEvent *builtin = nullptr;
    for (const BuiltinEvent &row : k_builtin_events) {
      if (row.name == event.name && row.min_args <= event.args.size()) {
        builtin = &row;
        break;
      }
    }
    if (builtin != nullptr) {
      builtin->handle(engine, gameplay, quest_runner, event);
    } else if (invoke_event_hook(gameplay, engine, event) == EventHookResult::NotHandled) {
      warn_log("[gameplay] WARN: unknown dialogue event '{}'", event.name);
    }
  } catch (...) {
    // Skip events whose processing throws (e.g. allocation failure); preserves the noexcept
    // contract of process_events.
    return;
  }
}

/** Start the fight start_encounter asked for, once the dialogue that asked has closed. */
void start_pending_encounter(Engine &engine, Gameplay &gameplay) {
  if (!gameplay.pending_encounter || engine.scene.mode() != world::GameMode::Exploring)
    return;
  const combat::EncounterRequest request = std::move(*gameplay.pending_encounter);
  gameplay.pending_encounter.reset();

  if (!gameplay.ruleset.start_fight) {
    warn_log("[gameplay] WARN: start_encounter ignored: the ruleset has no combat rules");
    return;
  }
  std::expected<combat::Battle, std::string> started =
      combat::Battle::start(engine, gameplay.ruleset.start_fight, request);
  if (!started) {
    refuse_fight(engine, started.error());
    return;
  }
  gameplay.battle.emplace(std::move(*started));
  engine.scene.ui.push(screens::Combat);
}

} // namespace corundum::gameplay
