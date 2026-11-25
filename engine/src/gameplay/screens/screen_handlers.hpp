// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
// The update and render handlers gameplay.cpp registers for each gameplay screen and render
// layer. Each is defined in its screen's .cpp, after that screen's layout and drawing code.
// A handler takes the whole Gameplay only when it calls a Gameplay command or touches more
// than two of its members; otherwise it takes just the members it reads and writes.

#include <corundum/core/math/vec.hpp>
#include <corundum/input/physical_input.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace corundum {

struct Engine;

} // namespace corundum

namespace corundum::input {

struct InputIntent;

} // namespace corundum::input

namespace corundum::platform {

class Renderer;

} // namespace corundum::platform

namespace corundum::gameplay {

class Gameplay;

} // namespace corundum::gameplay

namespace corundum::gameplay::codex {

class Registry;

} // namespace corundum::gameplay::codex

namespace corundum::gameplay::combat {

class Battle;

} // namespace corundum::gameplay::combat

namespace corundum::gameplay::dialogue {

class Conversation;

} // namespace corundum::gameplay::dialogue

namespace corundum::gameplay::location {

class Registry;

} // namespace corundum::gameplay::location

namespace corundum::gameplay::quest {

class Registry;

} // namespace corundum::gameplay::quest

namespace corundum::gameplay::screens {

struct CharacterInfo;
struct CharacterSheetState;
struct CodexState;
struct ConfirmState;
struct CreditsState;
struct DialogBoxState;
struct GameOverState;
struct JournalState;
struct MapState;
struct SaveLoadState;
struct SplashState;
struct TitleState;

// Menu hub (screens/hub_tabs.cpp)

/** The gameplay framework's pre-dispatch input hook: handles the menu hub's open/close/switch
 *  and tab cycling. Returns true when it consumed the step; safe to register as the only
 *  entry in `Engine::screen_input_hooks`.
 */
bool handle_hub_input(Engine &engine, Gameplay &gameplay, const input::InputIntent &intent);

/** The hub tab strip draws over every hub panel, in the top margin so it never overlaps one. */
void render_hub_strip(const Engine &engine, platform::Renderer &renderer, core::math::Vec2 viewport);

// HUD strip (screens/hud_strip.cpp)

/** Draw the HUD strip unless `dialogue` holds a conversation. The engine already shows the Hud
 *  layer only on an empty UI stack with no transition prompt, so the `dialogue` check is
 *  defensive.
 */
void render_hud(const Engine &engine, const std::optional<dialogue::Conversation> &dialogue,
                const quest::Registry &quests, platform::Renderer &renderer);

// Dialogue box (screens/dialog_box.cpp)

/** Modal dialogue box: draws only while a conversation is active and Dialogue is the top
 *  mode, so a screen pushed over it (the pause menu) never double-draws. Hiding clears
 *  visibility only — the cached layout and reveal progress are untouched, so popping the
 *  screen above restores the box exactly.
 */
void render_dialogue(const Engine &engine, DialogBoxState &state, const std::optional<dialogue::Conversation> &dialogue,
                     platform::Renderer &renderer, core::math::Vec2 viewport);

// Combat (screens/combat_hud.cpp)

/** Draw the target's tile marker, then the combat HUD with `banner` (empty for none). */
void render_combat(const Engine &engine, const combat::Battle &battle, platform::Renderer &renderer,
                   core::math::Vec2 viewport, std::string_view banner);

/** The paused fight's banner: "Paused" and the key that resumes it. */
std::string paused_banner(input::InputDevice device);

// Inventory (screens/inventory_panel.cpp)

/** Step the Inventory hub tab: Cancel closes it; a hovered row takes focus on a real mouse
 *  move or click; the wheel moves one row per notch; Up/Down wrap the highlight.
 */
void update_inventory(Engine &engine, Gameplay &gameplay, const input::InputIntent &intent);

void render_inventory(const Engine &engine, const Gameplay &gameplay, platform::Renderer &renderer,
                      core::math::Vec2 viewport);

// Character sheet (screens/character_sheet.cpp)

/** Handle the Character hotkey (P / gamepad Back): open the sheet from Exploring, close it
 *  when already on top. Any other top mode leaves the press alone, so the hotkey cannot
 *  stack the sheet over a dialogue or prompt.
 */
bool handle_character_input(Engine &engine, Gameplay &gameplay);

/** Step the character sheet: Cancel closes it, the navigation axes move the highlight, and
 *  Activate runs the highlighted row. The hotkey that opens it is handled by
 *  the input hook before this step runs.
 */
void update_character(Engine &engine, Gameplay &gameplay, const input::InputIntent &intent);

void render_character(const Engine &engine, const CharacterSheetState &state, const CharacterInfo &info,
                      platform::Renderer &renderer, core::math::Vec2 viewport);

// Journal (screens/journal.cpp)

/** Step the Journal hub tab: Cancel closes it; SubTabNext/Prev cycle sub-tabs (resetting the
 *  cursor); a click on the sub-tab strip selects a tab and the wheel over it cycles; a hovered
 *  row takes focus on a real mouse move or click; the wheel moves one row per notch; Activate
 *  toggles tracking on the highlighted Active quest; Up/Down wrap the highlight.
 */
void update_journal(Engine &engine, JournalState &state, const quest::Registry &quests,
                    const input::InputIntent &intent);

void render_journal(const Engine &engine, const JournalState &state, const quest::Registry &quests,
                    platform::Renderer &renderer, core::math::Vec2 viewport);

// Codex (screens/codex.cpp)

/** Step the Codex hub tab: Cancel closes it, Up/Down move the highlighted entry, the wheel
 *  over the list moves the cursor and over the detail body scrolls it, and a hovered list row
 *  takes focus on a real mouse move or click. Rows are refreshed from the registry + flags on
 *  the first step after an open or unlock (dirty-flagged). The Codex hotkey is handled by the
 *  hub table, which toggles this tab off before this step would run.
 */
void update_codex(Engine &engine, CodexState &state, const codex::Registry &codex, const input::InputIntent &intent);

void render_codex(const Engine &engine, const CodexState &state, platform::Renderer &renderer,
                  core::math::Vec2 viewport);

// Map (screens/map.cpp)

/** Step the Map hub tab: Cancel closes it, a hovered row takes focus on a real mouse move or
 *  click, Up/Down and the wheel move the highlighted destination, and Activate fast-travels.
 *  A destination already in the active zone is a no-op.
 */
void update_map(Engine &engine, MapState &state, const location::Registry &locations, const input::InputIntent &intent);

void render_map(const Engine &engine, const MapState &state, const location::Registry &locations,
                platform::Renderer &renderer, core::math::Vec2 viewport);

// Loot (screens/loot.cpp)

/** Step the loot screen: Back closes, Left/Right or a click switch the active pane, a hovered
 *  row takes focus on a real mouse move or click, the wheel moves one row per notch, and
 *  Activate moves one unit of the highlighted item to the other holder.
 */
void update_loot(Engine &engine, Gameplay &gameplay, const input::InputIntent &intent);

void render_loot(const Engine &engine, const Gameplay &gameplay, platform::Renderer &renderer,
                 core::math::Vec2 viewport);

// Barter (screens/barter.cpp)

/** Step the barter screen: Back closes, Up/Down move the row, a hovered row takes focus on a
 *  real mouse move or click, the wheel moves one row per notch, and Activate buys.
 */
void update_barter(Engine &engine, Gameplay &gameplay, const input::InputIntent &intent);

void render_barter(const Engine &engine, const Gameplay &gameplay, platform::Renderer &renderer,
                   core::math::Vec2 viewport);

// Confirm (screens/confirm.cpp)

/** Step the shared confirmation modal: Back closes it without acting; Left/Right (or
 *  Up/Down) toggle the highlight; Activate runs `on_yes` when Yes is highlighted. The
 *  callback is moved out and the state cleared before it runs, so it may itself open a
 *  screen or reset the session without racing the modal it just answered.
 */
void update_confirm(Engine &engine, Gameplay &gameplay, const input::InputIntent &intent);

/** Draw the confirmation modal while Confirm is the top mode. Pure render; `prompt_box_render`
 *  owns the layout so there is nothing to share with hit-testing.
 */
void render_confirm(const Engine &engine, const ConfirmState &state, platform::Renderer &renderer,
                    core::math::Vec2 viewport);

// Splash (screens/splash.cpp)

/** Step the Splash screen. `engine.screen_transition` carries each image's fade: the image
 *  fades in from black, holds for `k_splash_hold_seconds`, then fades out, each fade taking
 *  `k_splash_fade_seconds`. The first image first waits out `k_splash_initial_delay_seconds`
 *  at black. Once the fade-out reaches black, the next image (or the first screen) takes over.
 *  Activate, Cancel or a click jumps straight to the fade-out, including from part-way through
 *  the fade-in or the initial delay.
 */
void update_splash(Engine &engine, Gameplay &gameplay, const input::InputIntent &intent);

void render_splash(const Engine &engine, const SplashState &state, platform::Renderer &renderer,
                   core::math::Vec2 viewport);

// Title (screens/title.cpp)

/** Step the Title screen: a hovered enabled row takes focus on a real mouse move or click,
 *  Up/Down move the highlight (skipping Continue when no save exists), and Activate runs the
 *  highlighted command. Cancel does nothing — Title is the bottom of the framing flow.
 */
void update_title(Engine &engine, Gameplay &gameplay, const input::InputIntent &intent);

void render_title(const Engine &engine, const TitleState &state, platform::Renderer &renderer,
                  core::math::Vec2 viewport);

// Save/Load (screens/save_load.cpp)

/** Step the Save/Load screen: Cancel closes it; Up/Down and the wheel move the highlighted
 *  slot; Activate saves (confirming an overwrite) or loads a valid slot. Empty and corrupt
 *  slots cannot be loaded.
 */
void update_save_load(Engine &engine, Gameplay &gameplay, const input::InputIntent &intent);

void render_save_load(const Engine &engine, const SaveLoadState &state, platform::Renderer &renderer,
                      core::math::Vec2 viewport);

// Game over (screens/game_over.cpp)

/** Step the Game over screen: a hovered enabled row takes focus, Up/Down move the highlight,
 *  and Activate reloads the last slot, opens the Load screen, or returns to Title.
 */
void update_game_over(Engine &engine, Gameplay &gameplay, const input::InputIntent &intent);

void render_game_over(const Engine &engine, const GameOverState &state, platform::Renderer &renderer,
                      core::math::Vec2 viewport);

// Loading (screens/loading.cpp)

/** Draw "Loading..." and record that it happened, so the next step may run the queued work. */
void render_loading(const Engine &engine, Gameplay &gameplay, platform::Renderer &renderer, core::math::Vec2 viewport);

// Credits (screens/credits.cpp)

/** Step the Credits screen: Cancel closes it, otherwise the roll holds still until
 *  `state.hold` runs out, then the text scrolls upward at a fixed rate per step until the last
 *  line reaches the vertical center.
 */
void update_credits(Engine &engine, CreditsState &state, const input::InputIntent &intent);

void render_credits(const Engine &engine, const CreditsState &state, platform::Renderer &renderer,
                    core::math::Vec2 viewport);

} // namespace corundum::gameplay::screens
