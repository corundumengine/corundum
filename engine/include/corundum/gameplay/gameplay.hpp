// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// The gameplay framework umbrella: game entry points include this to get Gameplay, Runtime and
// the engine symbols they drive. engine.hpp / engine_factory.hpp supply the engine; the
// using-declarations at the bottom lift the names game code names directly.
#include <corundum/engine.hpp>         // IWYU pragma: export
#include <corundum/engine_factory.hpp> // IWYU pragma: export
#include <corundum/gameplay/codex/registry.hpp>
#include <corundum/gameplay/dialogue/action.hpp> // IWYU pragma: export
#include <corundum/gameplay/dialogue/registry.hpp>
#include <corundum/gameplay/item/registry.hpp>
#include <corundum/gameplay/location/registry.hpp>
#include <corundum/gameplay/quest/registry.hpp>
#include <corundum/gameplay/runtime.hpp> // IWYU pragma: export
#include <corundum/gameplay/screens/barter.hpp>
#include <corundum/gameplay/screens/codex.hpp>
#include <corundum/gameplay/screens/dialog_box.hpp>
#include <corundum/gameplay/screens/loot.hpp>
#include <corundum/gameplay/screens/map.hpp>
#include <corundum/gameplay/shop/registry.hpp>
#include <corundum/world/flags.hpp> // IWYU pragma: export

#include <functional>
#include <string>

namespace corundum {

  struct Engine;

} // namespace corundum

namespace corundum::gameplay {

  /** @brief The reusable-but-optional CRPG gameplay framework, layered above the engine runtime.
   *
   *  Owns the data-driven content registries, the gameplay screen state, and the per-step
   *  gameplay systems. Constructed with the `Engine&` it drives (constructor injection) and
   *  registers its screens, render-layer hooks and fixed-step system with that engine. Every
   *  hook it registers captures `this`, so a Gameplay must outlive its engine — the Runtime
   *  member order guarantees that.
   *
   *  Game-specific rules (stats, combat, skill checks) do not belong here; they layer above the
   *  framework, in the game itself.
   */
  class Gameplay {
  public:
    /** @brief Load content from @p engine's config and register with it.
     *
     *  @param[in] engine The engine to drive; must outlive this Gameplay.
     */
    explicit Gameplay(Engine &engine);

    ~Gameplay();

    Gameplay(const Gameplay &) = delete;
    Gameplay &operator=(const Gameplay &) = delete;
    Gameplay(Gameplay &&) = delete;
    Gameplay &operator=(Gameplay &&) = delete;

    dialogue::Registry graphs;

    item::Registry items;

    codex::Registry codex;

    location::Registry locations;

    quest::Registry quests;

    shop::Registry shops;

    /** @brief Codex-screen state: highlighted row and the dirty-flagged unlocked-entry cache. */
    screens::CodexState codex_screen;

    /** @brief Map-screen state: highlighted fast-travel destination. */
    screens::MapState map_screen;

    /** @brief Loot-screen state: active pane and highlighted row. */
    screens::LootState loot_screen;

    /** @brief Barter-screen state: active tab and highlighted row. */
    screens::BarterState barter_screen;

    /** @brief Dialogue-box reveal/layout state; stepped by the gameplay fixed-step system. */
    screens::DialogBoxState dialog_box;

    /** @brief Container whose contents the loot screen shows; empty when no loot screen is open. */
    std::string active_container_id;

    /** @brief Shop the barter screen trades with; empty when no barter screen is open. */
    std::string active_shop_id;

    /** @brief The game's single dialogue-event hook, consulted for actions the built-in
     *  dispatch does not handle. Return @c true to mark the event handled.
     *
     *  This is the game's slot, never overwritten by the framework. */
    std::function<bool(Engine &, const dialogue::EventAction &)> on_event;

    /** @brief Process every pending dialogue EventAction (built-in dispatch + on_event).
     *
     *  Clears scene.pending_dialogue_events after processing. Exposed for testability; the
     *  gameplay fixed-step system calls it once per step.
     */
    void process_events() noexcept;

  private:
    Engine *engine_{nullptr};
  };

  using dialogue::EventAction;

} // namespace corundum::gameplay

namespace corundum {

  using gameplay::EventAction;

  using world::set_flag;

} // namespace corundum
