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
#include <corundum/gameplay/screens/character_sheet.hpp>
#include <corundum/gameplay/screens/codex.hpp>
#include <corundum/gameplay/screens/confirm.hpp>
#include <corundum/gameplay/screens/credits.hpp>
#include <corundum/gameplay/screens/dialog_box.hpp>
#include <corundum/gameplay/screens/game_over.hpp>
#include <corundum/gameplay/screens/inventory_panel.hpp>
#include <corundum/gameplay/screens/journal.hpp>
#include <corundum/gameplay/screens/loot.hpp>
#include <corundum/gameplay/screens/map.hpp>
#include <corundum/gameplay/screens/modes.hpp>
#include <corundum/gameplay/screens/save_load.hpp>
#include <corundum/gameplay/screens/splash.hpp>
#include <corundum/gameplay/screens/title.hpp>
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

  /** @brief State that belongs to one playthrough. Replaced wholesale (never field by field) when
   *  a playthrough begins or ends, so no value can leak into the next one. The playthrough's game
   *  state proper lives in Engine::flags, bounded by the same calls. */
  struct Playthrough {
    /** @brief Seconds of gameplay so far; written into every save's SaveMeta and restored on load.
     *  Advanced by the gameplay fixed-step system, so it does not advance while a step-owning
     *  screen or the pause menu is open. */
    double playtime_seconds{};

    /** @brief Slot id of the most recent save or load (`quicksave`, `autosave`, `slot_03` …),
     *  or empty when this playthrough has neither saved nor loaded. */
    std::string last_slot{};
  };

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

    /** @brief Splash-screen state: the boot images, the one showing and how long it has held.
     *  Empty once the splash has closed. */
    screens::SplashState splash_screen;

    /** @brief Title screen state: highlighted row, Continue availability and whether the
     *  configured credits file loaded (which decides whether the Credits row is shown). */
    screens::TitleState title_screen;

    /** @brief Credits-screen state: sections loaded when the Title opened and the scroll offset. */
    screens::CreditsState credits_screen;

    /** @brief Game-over screen state: highlighted row and Reload availability. */
    screens::GameOverState game_over_screen;

    /** @brief Save/Load screen state: mode, highlighted row and the buffered slot rows. */
    screens::SaveLoadState save_load_screen;

    /** @brief Character-sheet values, sampled from the FlagStore when the sheet opens. The
     *  simulation is paused while it is open, so there is no per-frame rebuild. */
    screens::CharacterInfo character_sheet_info;

    /** @brief Character-sheet cursor: the highlighted section and row within it. Reset each time
     *  the sheet opens so it never lands on a row that has since disappeared. */
    screens::CharacterSheetState character_sheet_state;

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

    /** @brief The current playthrough's own state; reset by new_game(), load_from_slot() and
     *  return_to_title(). */
    Playthrough playthrough{};

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
     *  @post On success playthrough.last_slot is `quicksave`.
     */
    [[nodiscard]] std::expected<void, std::string> quick_save();

    /** @brief Load the `quicksave` slot and toast the outcome.
     *
     *  fixed_step() calls this on the QuickLoad action (F9); game code may also call it directly.
     *
     *  @return ok, or the load error (also surfaced as a failure toast).
     *  @post On success playthrough.last_slot is `quicksave`.
     */
    [[nodiscard]] std::expected<void, std::string> quick_load();

    /** @brief Write the `autosave` slot.
     *
     *  The framework never calls this; the game decides when to autosave (e.g. on a completed
     *  area transition). Writes no toast — the game decides whether to surface one.
     *
     *  @return ok, or the save error.
     *  @post On success playthrough.last_slot is `autosave`.
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

    /** @brief Queue @p work behind the Loading overlay and push GameMode Loading.
     *
     *  Loads are synchronous, so the work is run only after the Loading screen has drawn once:
     *  the player sees "Loading..." before a potentially slow scene rebuild. The work runs on
     *  the next step that follows a render (see advance_loading()), never before. Used by New
     *  Game, Continue and Load; portal transitions do not use it.
     *
     *  @param work Called with this Gameplay once the overlay has rendered; returns ok, or an
     *              error surfaced as a failure toast after Loading is popped.
     *  @post scene.ui.top() is screens::Loading and the work has not run.
     */
    void begin_load(std::function<std::expected<void, std::string>(Gameplay &)> work);

    /** @brief Record that the Loading overlay has been drawn.
     *
     *  Called by the Loading screen's render; without it a long frame that runs several fixed
     *  steps before the first render would run the pending work with nothing on screen.
     */
    void notify_loading_rendered() noexcept;

    /** @brief Run the pending load once the overlay has rendered, popping Loading first.
     *
     *  Called by the Loading screen's update. A no-op until notify_loading_rendered() has been
     *  called and a load is pending. On failure an error toast is raised with the work's
     *  message; Loading is already popped, so the player returns to the screen that started it.
     */
    void advance_loading();

    /** @brief Move the splash past the image that has just faded out to black.
     *
     *  Fades the next image in. After the last image, releases every splash texture, pops
     *  GameMode Splash, opens the first screen (the Title, or a new playthrough when
     *  GameConfig::show_title is false) and fades it in from black.
     *
     *  @pre scene.ui.top() is screens::Splash and Engine::screen_transition is at black.
     */
    void advance_splash();

    /** @brief Show the Credits screen with the sections loaded when the Title opened.
     *
     *  Pushes GameMode Credits. Only reachable from the Title, and only when
     *  TitleState::credits_available is true (open_title() hides the row otherwise).
     *
     *  @post scene.ui.top() is screens::Credits.
     */
    void open_credits();

    /** @brief Show the title screen with no world loaded.
     *
     *  Refreshes Continue availability from the saves directory, puts the cursor on the first
     *  enabled row, and pushes GameMode Title. Called at boot (after any splash) when GameConfig::show_title
     *  is set and again whenever the player returns to the title.
     *
     *  @note The title runs with no world loaded: boot builds none, and return_to_title() unloads
     *  it. The screen still draws an opaque backdrop.
     *  @post engine.scene.ui.top() is screens::Title.
     */
    void open_title();

    /** @brief Show the character sheet over the current scene.
     *
     *  Samples the character sheet's values from the FlagStore and pushes GameMode Character.
     *  Only reachable from Exploring (the hotkey handler ignores it while another screen is
     *  open).
     *
     *  @post engine.scene.ui.top() is screens::Character.
     */
    void open_character_sheet();

    /** @brief Show the game-over screen.
     *
     *  The game raises it (from a dialogue event or its own on_fixed_update), never the
     *  framework. Reload availability follows playthrough.last_slot.
     *
     *  @post engine.scene.ui.top() is screens::GameOver.
     */
    void open_game_over();

    /** @brief Show the Save/Load slot browser in save (@p saving true) or load mode.
     *
     *  Buffers the slot rows for the engine's saves directory and pushes GameMode SaveLoad on
     *  top of whatever is currently open, so Back returns there.
     *
     *  @post engine.scene.ui.top() is screens::SaveLoad.
     */
    void open_save_load(bool saving);

    /** @brief End the playthrough and show the title: clear session state, unload the world, clear
     *  the flags and the Playthrough record, then open_title(). Used by Quit to Title, Game over
     *  "Return to Title", and as the fallback when a new game or load cannot build its world.
     *
     *  @post engine.render.mode is RenderMode::None, engine.flags is empty and
     *        engine.scene.ui.top() is screens::Title.
     */
    void return_to_title();

    /** @brief Start a fresh playthrough (begin_new_playthrough), then fade it in with a toast.
     *
     *  On failure, return_to_title() and a failure toast.
     *
     *  @return ok, or the scene-rebuild error.
     *  @post On success engine.scene.ui is empty and the scene is the configured initial scene.
     */
    [[nodiscard]] std::expected<void, std::string> new_game();

    /** @brief Load the newest valid save slot and toast the outcome.
     *
     *  @return ok when a slot was loaded, or the error; an empty save list is an error.
     *  @post On success playthrough.last_slot is the loaded slot's id.
     */
    [[nodiscard]] std::expected<void, std::string> continue_game();

    /** @brief Write @p slot_id under the saves directory, stamping location and playtime.
     *
     *  Exposed so the Save/Load screen can write a player-chosen slot; quick_save()/autosave()
     *  route through it.
     *
     *  @post On success playthrough.last_slot is @p slot_id. */
    [[nodiscard]] std::expected<void, std::string> save_to_slot(std::string_view slot_id);

    /** @brief Load @p slot_id from the saves directory.
     *
     *  Exposed so the Save/Load screen can load a player-chosen slot; quick_load() routes
     *  through it.
     *
     *  @post On success playthrough.last_slot is @p slot_id. */
    [[nodiscard]] std::expected<void, std::string> load_from_slot(std::string_view slot_id);

    /** @brief Return every *screen*-scoped member to its freshly-constructed state.
     *
     *  Called after every scene replacement the framework triggers (New Game, Continue, Load,
     *  Return to Title, Game over Reload). Clears the active conversation and bound NPC, the
     *  active container and shop ids, every screen cursor and cache, the confirm modal and the
     *  UI stack. Content registries and scene flags are deliberately untouched — they are
     *  loaded content and world state, not screen state. The playthrough tier ends in
     *  return_to_title(), not here.
     *
     *  @post engine.scene.ui is empty and no screen holds a stale cursor or cache.
     */
    void reset_session_state();

  private:
    /** @brief Begin a fresh playthrough: reset flags to GameConfig::starting_flags, clear the
     *  Playthrough record and entered_from_world, build the configured initial scene, and clear
     *  session state. Shows no fade or toast; new_game() and the no-title boot add those.
     *  @return ok, or the scene-build error (the world may then be half-built). */
    [[nodiscard]] std::expected<void, std::string> begin_new_playthrough();

    /** @brief Load GameConfig's splash_images; when at least one loads, push GameMode Splash and
     *  start its fade-in from black. An image that fails to load is skipped with one warning.
     *  @return true when the splash opened, false when there is nothing to show (or no renderer). */
    [[nodiscard]] bool open_splash();

    /** @brief Open what the boot leads to: the Title, or with GameConfig::show_title false a new
     *  playthrough, falling back to the Title when its scene cannot be built. Shows no fade or toast. */
    void open_first_screen();

    /** @brief Load `cfg.paths.credits_file` into credits_screen, recording whether it succeeded
     *  in TitleState::credits_available. A missing path, unreadable file or malformed document
     *  hides the Credits row; a non-empty path that fails logs one warning. A non-empty
     *  background path is loaded through the renderer, logging one warning and leaving the flat
     *  backdrop when the image fails. */
    void refresh_credits();

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

    /** @brief Work queued by begin_load(), run by advance_loading() after the first Loading
     *  render. Empty when no load is pending. */
    std::function<std::expected<void, std::string>(Gameplay &)> pending_load_{};

    /** @brief True once the Loading screen has rendered its first frame; gates advance_loading(). */
    bool loading_rendered_{false};
  };

  using dialogue::EventAction;

} // namespace corundum::gameplay

namespace corundum {

  using gameplay::EventAction;

  using world::set_flag;

} // namespace corundum
