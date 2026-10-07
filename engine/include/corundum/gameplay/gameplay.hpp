// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// The gameplay framework umbrella: game entry points include this to get Gameplay, Runtime and
// the engine symbols they drive. engine.hpp / engine_factory.hpp supply the engine; the
// using-declarations at the bottom lift the names game code names directly.
#include <corundum/engine.hpp>         // IWYU pragma: export
#include <corundum/engine_factory.hpp> // IWYU pragma: export
#include <corundum/entities/entity.hpp>
#include <corundum/gameplay/codex/registry.hpp>
#include <corundum/gameplay/dialogue/action.hpp> // IWYU pragma: export
#include <corundum/gameplay/dialogue/conversation.hpp>
#include <corundum/gameplay/dialogue/dialogue_npc.hpp>
#include <corundum/gameplay/dialogue/registry.hpp>
#include <corundum/gameplay/item/registry.hpp>
#include <corundum/gameplay/location/registry.hpp>
#include <corundum/gameplay/quest/registry.hpp>
#include <corundum/gameplay/runtime.hpp> // IWYU pragma: export
#include <corundum/gameplay/screens/barter.hpp>
#include <corundum/gameplay/screens/codex.hpp>
#include <corundum/gameplay/screens/confirm.hpp>
#include <corundum/gameplay/screens/dialog_box.hpp>
#include <corundum/gameplay/screens/inventory_panel.hpp>
#include <corundum/gameplay/screens/journal.hpp>
#include <corundum/gameplay/screens/loot.hpp>
#include <corundum/gameplay/screens/map.hpp>
#include <corundum/gameplay/screens/modes.hpp>
#include <corundum/gameplay/shop/registry.hpp>
#include <corundum/world/flags.hpp> // IWYU pragma: export
#include <corundum/world/ui_stack.hpp>

#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace corundum {

  struct Engine;

} // namespace corundum

namespace corundum::input {
  struct InputIntent;
}

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

    /** @brief Shared yes/no confirmation modal; pushed by open_confirm(). */
    screens::ConfirmState confirm;

    /** @brief Dialogue-box reveal/layout state; stepped by the gameplay fixed-step system. */
    screens::DialogBoxState dialog_box;

    /** @brief Active dialogue conversation; disengaged while not in a dialogue. Owned here so the
     *  presentation layer can query it read-only; stepped by the gameplay fixed-step system. */
    std::optional<dialogue::Conversation> dialogue;

    /** @brief The NPC bound to the active dialogue, saved so its facing/animation can be restored. */
    std::optional<dialogue::DialogueNpc> dialogue_npc;

    /** @brief Dialogue events emitted by the current step, consumed by process_events(). */
    std::vector<dialogue::EventAction> pending_dialogue_events;

    /** @brief Highlighted row while the Inventory hub tab is open; wrapped against inventory_lines. */
    int inventory_cursor{};

    /** @brief First visible item row while the Inventory hub tab is open; kept so the highlighted
     *  row stays inside the list window. */
    int inventory_scroll{};

    /** @brief Held-item rows of the Inventory hub tab, rebuilt when the tab is opened or switched
     *  to. The inventory is read-only and the simulation is paused while it is open, so there is
     *  no per-frame rebuild (see AGENTS.md, "Cache or hoist per-frame-invariant computation"). */
    std::vector<screens::InventoryLine> inventory_lines;

    /** @brief Equipment-slot rows of the Inventory hub tab's left column, rebuilt alongside
     *  inventory_lines. The game owns the `equip.<slot>.<item id>` flags; this only displays them. */
    std::vector<screens::EquipmentLine> inventory_equipment;

    /** @brief State of the Journal hub tab: its active sub-tab and highlighted row. The cursor
     *  wraps against the rows of the active sub-tab. */
    screens::JournalState journal_screen;

    /** @brief Hub tab the Hub button (gamepad Y) reopens; the last tab that was opened or switched
     *  to. */
    world::GameMode last_hub_mode{screens::Inventory};

    /** @brief Seconds of gameplay accumulated across scene replacements; written into every
     *  save's SaveMeta. Advanced by the gameplay fixed-step system, so it does not advance while
     *  a step-owning screen or the pause menu is open. */
    double playtime_seconds{};

    /** @brief Slot id of the most recent save or load (`quicksave`, `autosave`, `slot_03` …),
     *  or empty when this session has neither saved nor loaded. */
    std::string last_slot{};

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
     *  Clears pending_dialogue_events after processing. Exposed for testability; fixed_step()
     *  calls it once per step.
     */
    void process_events() noexcept;

    /** @brief The gameplay framework's per-fixed-step entry point: dialogue update or
     *  interaction hand-off, event processing, dialogue-box advance, then the quest tick.
     *
     *  Registered as the engine's single gameplay fixed-step system, so the engine runtime never
     *  names a gameplay registry. Runs after world::update and before the game's
     *  on_fixed_update.
     */
    void fixed_step(float dt);

    /** @brief Save the current state to the `quicksave` slot and toast the outcome.
     *
     *  fixed_step() calls this on the QuickSave action (F5); game code may also call it directly.
     *
     *  @return ok, or the save error (also surfaced as a failure toast).
     *  @post On success last_slot is `quicksave`.
     */
    [[nodiscard]] std::expected<void, std::string> quick_save();

    /** @brief Load the `quicksave` slot and toast the outcome.
     *
     *  fixed_step() calls this on the QuickLoad action (F9); game code may also call it directly.
     *
     *  @return ok, or the load error (also surfaced as a failure toast).
     *  @post On success last_slot is `quicksave`.
     */
    [[nodiscard]] std::expected<void, std::string> quick_load();

    /** @brief Write the `autosave` slot.
     *
     *  The framework never calls this; the game decides when to autosave (e.g. on a completed
     *  area transition). Writes no toast — the game decides whether to surface one.
     *
     *  @return ok, or the save error.
     *  @post On success last_slot is `autosave`.
     */
    [[nodiscard]] std::expected<void, std::string> autosave();

    /** @brief Open the shared yes/no confirmation modal carrying @p question.
     *
     *  Pushes GameMode Confirm; Yes runs @p on_yes and No and Back close it without running
     *  anything. Game code and the framework's Quit-to-Title path both route through here.
     *
     *  @param question Prompt drawn on the top line; shown verbatim in one line.
     *  @param on_yes   Called with this Gameplay after Confirm is popped; may be empty.
     *  @post scene.ui.top() is screens::Confirm.
     */
    void open_confirm(std::string question, std::function<void(Gameplay &)> on_yes);

    /** @brief Return every session-scoped member to its freshly-constructed state.
     *
     *  Called after every scene replacement the framework triggers (New Game, Continue, Load,
     *  Return to Title, Game over Reload). Clears the active conversation and bound NPC, the
     *  active container and shop ids, every screen cursor and cache, the confirm modal and the
     *  UI stack. Content registries and scene flags are deliberately untouched — they are
     *  loaded content and world state, not session state.
     *
     *  @post engine.scene.ui is empty and no screen holds a stale cursor or cache.
     */
    void reset_session_state();

  private:
    /** @brief Write @p slot_id under the saves directory, stamping location and playtime.
     *  @post On success last_slot is @p slot_id. */
    [[nodiscard]] std::expected<void, std::string> save_to_slot(std::string_view slot_id);

    /** @brief Load @p slot_id from the saves directory.
     *  @post On success last_slot is @p slot_id. */
    [[nodiscard]] std::expected<void, std::string> load_from_slot(std::string_view slot_id);

    /** @brief The location registry's display name for the current zone, or the raw zone id
     *  when unknown. */
    [[nodiscard]] std::string current_location_name() const;

    /** @brief Advance the active dialogue conversation, restoring the bound NPC's facing and
     *  animation when it ends and closing the Dialogue screen.
     *
     *  @pre The top UI mode is screens::Dialogue.
     */
    void update_dialogue(const input::InputIntent &intent);

    /** @brief Start a dialogue with @p target: bind it, face it toward the player, and open the
     *  Dialogue screen. A target with no loaded graph is ignored.
     *
     *  @pre The top UI mode is Exploring and @p target names a live interactable entity.
     */
    void try_interact(corundum::entities::EntityId target);

    Engine *engine_{nullptr};
  };

  using dialogue::EventAction;

} // namespace corundum::gameplay

namespace corundum {

  using gameplay::EventAction;

  using world::set_flag;

} // namespace corundum
