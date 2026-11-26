// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Each include below supplies a complete type for an Engine member (or an inline
// symbol the public API uses); do not slim this block.
#include <corundum/audio/audio_system.hpp>
#include <corundum/core/content_source.hpp>
#include <corundum/core/game_config.hpp>
#include <corundum/core/math/vec.hpp>
#include <corundum/core/rng.hpp>
#include <corundum/core/time/loop_timer.hpp>
#include <corundum/core/user_storage.hpp>
#include <corundum/core/verify.hpp>
#include <corundum/debug/debug_overlay.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/input_mapper.hpp>
#include <corundum/platform/gpu_context.hpp>
#include <corundum/platform/handle.hpp>
#include <corundum/platform/platform_events.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/platform/window.hpp>
#include <corundum/render/render_state.hpp>
#include <corundum/screen_registry.hpp>
#include <corundum/sprites/character_registry.hpp>
#include <corundum/ui/menu.hpp>
#include <corundum/ui/screen_transition.hpp>
#include <corundum/ui/settings.hpp>
#include <corundum/ui/toast.hpp>
#include <corundum/world/flags.hpp>
#include <corundum/world/scene.hpp>
#include <corundum/world/tilemap/tilemap.hpp>
#include <corundum/world/transition.hpp>
#include <corundum/world/ui_stack.hpp>

#include <bitset>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace corundum {

namespace platform {
struct PlatformContext;
} // namespace platform

namespace input {
struct InputIntent;
} // namespace input

/** Simulation run state.
 *
 *  Running advances the fixed-step simulation. Paused, while any PauseReason holds, keeps it
 *  still and silences audio; run_frame() still polls platform events and renders while paused.
 */
enum class RunState : std::uint8_t {
  Running,
  Paused,
};

/** Why the simulation is paused. Several can hold at once; the engine runs only when none do.
 *
 *  Controller clears on a press, not on movement: moving the mouse or brushing the desk is not the
 *  player choosing to continue on keyboard and mouse. Like every resume, the press that clears it is
 *  consumed and does not act in the game, even when it is the fullscreen key.
 */
enum class PauseReason : std::uint8_t {
  /** The window lost focus or was minimised; cleared when focus returns. */
  Focus,

  /** The active gamepad disconnected with no pad left; cleared on reconnect or a keyboard/mouse press. */
  Controller,

  /** Requested by game code (e.g. a pause menu); only game code clears it. */
  Game,
  Count,
};

/** Which Save/Load command the pause menu raised.
 *
 *  Carried by `Engine::Hooks::on_menu_save_load` and `Gameplay::open_save_load` so the two modes are a
 *  named choice rather than a bare bool.
 */
enum class SaveLoadIntent : std::uint8_t {
  Save,
  Load,
};

/** Game engine instance owning all system-level resources and game state.
 *
 *  Owns systems directly (no virtual dispatch), the Scene (merged entity world +
 *  game state), and all game assets. Lifecycle: initialize(), then run_loop() (input,
 *  fixed-step simulation, rendering), then cleanup().
 *
 *  make_engine() heap-allocates it: the embedded entity World is ~0.5 MB and must never sit
 *  on a stack frame. Members must still never store pointers or references into sibling
 *  members; the one exception is the platform adapters' reference to the content source
 *  (`content_`), which member order keeps alive.
 */
// Member order is deliberate — Scene first for 64-byte alignment, window→gpu→renderer for
// reverse-order teardown, and the rest grouped by subsystem. Reordering for the 64 bytes the
// check finds would interleave private state and defeat that grouping on a ~0.5 MB heap object.
// NOLINTNEXTLINE(clang-analyzer-optin.performance.Padding)
struct Engine {
  /** Declared first: Scene's 64-byte alignment packs cleanly at offset 0 and keeps the rest dense. */
  world::Scene scene;

  /** Read-only game content: every runtime load reads through this source.
   *
   *  Defaults to the working directory with paths as given.
   */
  [[nodiscard]] const core::ContentSource &content_source() const noexcept { return *content_; }

  /** Replace the default content source with `source` before a platform is adopted.
   *
   *  @param source New source (move-ownership); must be non-null.
   *  @return ok, or an error when a platform is already adopted — the platform adapters hold
   *          a reference to the current source and would dangle.
   */
  [[nodiscard]] std::expected<void, std::string> set_content_source(std::unique_ptr<core::ContentSource> source);

  /** Per-user storage for settings and saves, or why there is none.
   *
   *  None when the project has no game_id or no user-data directory. Without storage, settings
   *  persistence is skipped and saves report the error.
   */
  std::expected<std::unique_ptr<core::UserStorage>, std::string> user_storage{
      std::unexpected(std::string{"user storage is not configured"})};

private:
  /** Declared before window/gpu/renderer/audio so it outlives the platform adapters, which keep a
   *  reference to it from create_platform(); set_content_source() refuses to replace it afterwards.
   */
  std::unique_ptr<core::ContentSource> content_{std::make_unique<core::DirectoryContentSource>()};

  /** Declared window → gpu → renderer, after content_, so reverse-order destruction is renderer → gpu →
   *  window → content: sokol resources are released while the device is still alive, the device and
   *  window outlive the renderer, and the content source outlives the adapters that read through it.
   */
  platform::Handle<platform::Window> window_;

  platform::Handle<platform::GpuContext> gpu_;

  platform::Handle<platform::Renderer> renderer_;

public:
  /** The adopted window. @pre adopt_platform() has run (make_engine() runs it). */
  [[nodiscard]] platform::Window &window() noexcept {
    core::verify(window_ != nullptr, "Engine::window: no platform adopted");
    return *window_;
  }

  [[nodiscard]] const platform::Window &window() const noexcept {
    core::verify(window_ != nullptr, "Engine::window: no platform adopted");
    return *window_;
  }

  /** The adopted renderer. @pre adopt_platform() has run (make_engine() runs it).
   *
   *  `const` because a screen's render hook sees a const Engine yet still draws through it: the
   *  handle is what the Engine owns, not the renderer's mutable drawing state.
   */
  [[nodiscard]] platform::Renderer &renderer() const noexcept {
    core::verify(renderer_ != nullptr, "Engine::renderer: no platform adopted");
    return *renderer_;
  }

  /** The adopted GPU context, or nullptr on a platform without one (the null platform). */
  [[nodiscard]] platform::GpuContext *gpu() noexcept { return gpu_.get(); }

  audio::AudioSystem audio;

  input::InputState input_state;

  /** Physical input → Action translation and the live binding table; see input::InputMapper. */
  input::InputMapper input_mapper;

  render::RenderState render;

  core::GameConfig config;

  sprites::CharacterRegistry characters;

  world::FlagStore flags;

  core::math::Colour clear_colour{.r = 30, .g = 30, .b = 35, .a = 255};

  debug::HudOverlay hud;

  /** Transient on-screen notifications, rendered bottom-left and aged by the fixed
   *  timestep. Engine quest events enqueue here; game code calls notify() for its own cues.
   */
  ui::ToastQueue toasts;

  /** Pause-menu selection state; rendered in GameMode::Menu. */
  ui::MenuState menu;

  /** Settings-screen state; rendered in GameMode::Settings. */
  ui::SettingsState settings_screen;

  /** Full-screen scene fade; driven by advance_scene_transition() and the loading path. */
  ui::ScreenTransition screen_transition;

  core::time::LoopTimer timer{static_cast<float>(core::k_default_simulation_fps)};

  /** Deterministic gameplay RNG, seeded at startup. All gameplay randomness must come from
   *  here so a run is reproducible from its seed.
   */
  core::Rng rng;

  /** Screen specs keyed by GameMode, plus the ordered render-layer hooks.
   *
   *  Engine-owned screens (Menu, Settings) register here during initialize(); the gameplay
   *  framework registers its own screens and layer hooks when gameplay::Gameplay is constructed
   *  over the engine. Dispatch happens in update_engine_screens() and the layered render
   *  sequence in render_frame().
   */
  ScreenRegistry screens;

  /** Callbacks through which game code, the gameplay framework and engine-runtime systems extend the engine.
   *
   *  A hook that receives the `Engine` may call anything on it, a save load or a return to title
   *  included. Each callable is a single slot, so setting one replaces whatever was installed.
   *  The two vectors are registration lists whose entries run in registration order; a
   *  `screen_input_handlers` entry that returns true consumes the step and ends the list.
   */
  struct Hooks {
    // Single slots

    /** Hook consulted before the pause menu opens on Action::Menu.
     *
     *  Receives the current top GameMode; returning true suppresses the menu open. The
     *  gameplay framework installs it so the menu does not open over the Title, Game over,
     *  Loading and Credits screens. Default-empty means the menu never blocks on a mode.
     */
    std::function<bool(world::GameMode)> blocks_pause_menu{};

    /** Hook called once per fixed step after the registered fixed_step_systems.
     *
     *  Invoked inside the fixed-timestep loop, after every fixed_step_systems entry
     *  (including gameplay's dialogue/event/quest tick) and before entity deletions are
     *  flushed. `dt` is the fixed timestep (timer.target_dt). Entities marked for deletion
     *  here are drained the same frame.
     */
    std::function<void(Engine &, float dt)> on_fixed_update{};

    /** Hook replacing the pause menu's Quit command.
     *
     *  When set, selecting Quit invokes it instead of request_quit(), so the game can
     *  interpose a confirmation or route the player to a framing screen. Unset, Quit calls
     *  request_quit() at once. The gameplay framework installs it to offer Quit to Title.
     */
    std::function<void(Engine &)> on_menu_quit{};

    /** Hook replacing the pause menu's Save and Load commands.
     *
     *  When set, selecting Save invokes it with SaveLoadIntent::Save and Load with
     *  SaveLoadIntent::Load, so the game can open a slot browser in the matching mode.
     *  Unset, Save and Load raise QuickSave / QuickLoad one step later. The gameplay framework
     *  installs it to open the Save/Load screen.
     */
    std::function<void(Engine &, SaveLoadIntent intent)> on_menu_save_load{};

    /** Hook called after the engine reacts to OS lifecycle events.
     *
     *  Invoked once per frame in which at least one PlatformEvents field is set,
     *  after focus, controller and quit handling, so game code observes the
     *  resulting run state. Unset, nothing is called.
     */
    std::function<void(Engine &, const platform::PlatformEvents &)> on_platform_event{};

    // Registration lists

    /** Extra per-fixed-step systems, run in registration order before on_fixed_update.
     *
     *  This is the slot for engine-runtime systems and framework systems — never the game's
     *  own hook. on_fixed_update stays the game's single slot. Each entry is a
     *  `void(Engine&, float dt)` callable; the gameplay framework registers exactly one system
     *  (dialogue update, event processing and the quest tick). Registered once, at construction
     *  time, so the std::function storage allocates only then.
     */
    std::vector<std::function<void(Engine &, float)>> fixed_step_systems{};

    /** Input hooks consulted by update_engine_screens() before ScreenRegistry dispatch.
     *
     *  An engine/framework system registers here to claim a step for a screen it opens or
     *  switches (the gameplay framework registers the menu hub here). Registered once, at
     *  initialize time, so the std::function storage allocates only then.
     */
    std::vector<std::function<bool(Engine &, const input::InputIntent &)>> screen_input_handlers{};
  };

  Hooks hooks{};

  /** True while inside an interior reached from the overworld. */
  bool entered_from_world{false};

  /** Take ownership of a complete platform bundle.
   *
   *  Moves the window, GPU context, renderer and audio backend out of `platform` in the
   *  order initialize() and the main loop expect; `platform` is left empty. Pass a
   *  platform::PlatformContext from create_platform().
   */
  void adopt_platform(platform::PlatformContext platform);

  /** Initialise all systems and load process-lifetime assets (sprites, fonts, UI skin,
   *  audio).
   *
   *  Loads no world: the game builds one with world::load_initial_scene() or
   *  world::apply_spawn() (the gameplay framework does so on New Game / Load, or at
   *  construction when GameConfig::show_title is false).
   *
   *  @param config Fully-loaded game configuration (move-ownership).
   *  @return ok on success, or std::unexpected with an error message.
   *  @pre window() and renderer() are non-null (make_engine() satisfies this).
   *       gpu() may be null (the main loop never touches it) for backends
   *       that don't need a GPU context.
   *  @post On failure, cleanup() has been run on the partially-initialised
   *        engine; discard it.
   */
  [[nodiscard]] std::expected<void, std::string> initialize(core::GameConfig &&config);

  /** Advance the engine by exactly one frame: poll input, run pending
   *  fixed steps, render once with interpolation.
   *
   *  @param now This frame's steady-clock reading. run_loop() passes
   *         std::chrono::steady_clock::now(); a test passes a fixed reading so the
   *         frame adds no wall time.
   *  @return true while the loop should continue; false once quit is requested
   *          or the window has closed. run_loop() is equivalent to
   *          while (run_frame(std::chrono::steady_clock::now())) {}.
   *
   *  @pre initialize() must have returned successfully.
   *  @post If no step-owning screen was on top during a fixed step, both `hooks.fixed_step_systems`
   *        and `hooks.on_fixed_update` ran. If a step-owning screen was on top, neither ran, and
   *        world::update and deletion flushing were skipped — which in turn skips the
   *        gameplay fixed-step system's dialogue update, event processing and quest tick.
   *        This holds whatever the top GameMode is.
   *  @note Allocates only for dialogue-event item/flag bookkeeping and, in World
   *        mode, for the one chunk streamed in per frame.
   */
  [[nodiscard]] bool run_frame(std::chrono::steady_clock::time_point now) noexcept;

  /** Run the main loop until the window closes or quit is requested.
   *
   *  Equivalent to while (run_frame(std::chrono::steady_clock::now())) {}.
   *
   *  @pre initialize() must have returned successfully.
   *  @note Allocation behaviour is that of run_frame(), once per iteration.
   */
  void run_loop() noexcept;

  /** Run the main loop followed by orderly teardown.
   *
   *  Equivalent to run_loop() followed by cleanup().
   *
   *  @pre initialize() must have returned successfully.
   */
  void run() noexcept;

  /** Step the top screen through the registry for one fixed step.
   *
   *  Runs the registered `hooks.screen_input_handlers` (which may consume the step by opening or
   *  switching a screen), then handles the pause-menu open input, then dispatches the top
   *  mode's ScreenSpec::update. Returns true when the step was claimed — by a registered input handler,
   *  by opening the pause menu, or by a registered mode whose spec owns the step — so the caller
   *  skips world::update and the rest of the simulation, which is what pauses the world while a
   *  menu is open.
   *
   *  Exposed for testability; run_frame() calls it once per fixed step through
   *  run_fixed_steps().
   */
  [[nodiscard]] bool update_engine_screens(const input::InputIntent &intent);

  /** Drive a pending portal scene change through a full-screen fade.
   *
   *  Called once per frame after the fixed steps. When a portal has armed
   *  `scene.pending_transition` it starts a fade-out and holds the swap; once the overlay is
   *  at_black() the scene change runs and the new scene fades in. A no-op otherwise, so UI
   *  screens never trigger a fade.
   */
  void advance_scene_transition() noexcept;

  /** Enqueue a transient on-screen notification with the default colour.
   *
   *  Drawn bottom-left and auto-dismissed after ui::k_toast_ttl_seconds. The engine enqueues
   *  quest-start/update/complete/failed messages itself; this is for game-specific cues.
   *
   *  @param text Message to display.
   */
  void notify(std::string text);

  /** Enqueue a transient on-screen notification in `colour`.
   *  @param text   Message to display.
   *  @param colour Text colour; use one of the ui::k_toast_*_colour constants for the standard tints.
   */
  void notify(std::string text, core::math::Colour colour);

  /** Queue `action` to be raised on the next fixed step that no screen owns.
   *
   *  For screen code that must hand an intent to the simulation without mutating
   *  input_state directly (e.g. the pause menu's Save/Load entries raise QuickSave/QuickLoad).
   *  The slot holds one action (last write wins) and lives outside input_state, so
   *  clear_pressed() cannot drop it. update_engine_screens() promotes it into
   *  input_state.pressed at the first step whose top screen does not own the step, keeping it
   *  pending while a step-owning screen (the pause menu, a hub tab, settings) is open. A
   *  lower bound of one step of latency is deliberate: the action is observed by
   *  `hooks.fixed_step_systems` and `hooks.on_fixed_update` on the step after it is raised, never by a
   *  screen's own Activate handling.
   *
   *  @param action Action to raise; ignored when it is not a valid Action index.
   */
  void raise_action_next_step(input::Action action) noexcept;

  /** Request a graceful shutdown.
   *
   *  Sets the quit flag; the next iteration of run_loop() will exit the main
   *  loop. Window closing is handled exclusively by cleanup(). Safe to call from
   *  any system during update().
   */
  void request_quit() noexcept;

  /** Switch the window between windowed and borderless fullscreen.
   *
   *  For an options menu or game code, applied from a safe point (e.g. the title screen), not
   *  mid-gameplay. initialize() applies GameConfig::window_mode directly.
   */
  void toggle_fullscreen() const noexcept;

  /** Tear down resources after the main loop exits.
   *
   *  Shuts down audio and closes the window. Safe to call multiple times; after
   *  the first call, the only valid operation on the Engine is destruction.
   *
   *  @pre run_loop() must have returned, or initialize() has failed.
   */
  void cleanup() noexcept;

  /** The single loaded tilemap, if the engine is in single-map mode.
   *
   *  Forwards to render::active_tilemap(), the RenderState-level accessor.
   *
   *  @return Pointer to the active tilemap, or nullptr in World mode or before load.
   */
  [[nodiscard]] const world::tilemap::Tilemap *active_tilemap() const noexcept {
    return render::active_tilemap(render);
  }

  /** True once request_quit() or cleanup() has been called. */
  [[nodiscard]] bool quit_requested() const noexcept { return quit_; }

  /** Current simulation run state: Paused while any PauseReason holds. */
  [[nodiscard]] RunState run_state() const noexcept { return is_paused() ? RunState::Paused : RunState::Running; }

  /** True while any PauseReason holds. */
  [[nodiscard]] bool is_paused() const noexcept { return pause_reasons_.any(); }

  /** True while `reason` holds. */
  [[nodiscard]] bool is_paused_for(PauseReason reason) const noexcept {
    core::verify(reason != PauseReason::Count, "is_paused_for: PauseReason::Count is not a reason");
    return pause_reasons_[static_cast<std::size_t>(reason)];
  }

  /** Add `reason`; the first reason pauses the simulation and audio. Idempotent. */
  void pause(PauseReason reason) noexcept;

  /** Remove `reason`. Idempotent.
   *
   *  Removing the last reason resumes. The loop timer's accumulator is cleared so the paused
   *  interval is not replayed as catch-up steps, and presses latched while paused are dropped, so
   *  the click that refocused the window or the key that dismissed a prompt does not also act in
   *  the game.
   */
  void resume(PauseReason reason) noexcept;

  /** Live window width in logical screen coordinates (0 before the first frame). */
  [[nodiscard]] int window_width() const noexcept { return window_width_; }

  /** Live window height in logical screen coordinates (0 before the first frame). */
  [[nodiscard]] int window_height() const noexcept { return window_height_; }

private:
  /** Reveal the window once the first frame is on screen, then apply the configured mode.
   *
   *  The window is created hidden, so the player never watches a blank window while assets load.
   *  GameConfig::window_mode is applied here rather than during initialize() because entering
   *  fullscreen while the window is still hidden leaves it hidden on macOS. Idempotent: only the
   *  first call reaches the platform.
   */
  void reveal_window() noexcept;

  /** Register the engine-owned screen specs.
   *
   *  Called once from initialize(). The gameplay framework registers its own screens,
   *  fixed-step system and layer hooks when gameplay::Gameplay is constructed over the engine.
   */
  void register_screens();

  /** Set by request_quit()/cleanup(); see quit_requested(). */
  bool quit_{false};

  /** One-slot action queued by raise_action_next_step(); promoted in update_engine_screens(). */
  std::optional<input::Action> pending_action_{};

  /** Cached each frame by run_frame(); see window_height(). */
  int window_height_{0};

  /** Cached each frame by run_frame(); see window_width(). */
  int window_width_{0};

  /** See pause()/resume(). */
  std::bitset<static_cast<std::size_t>(PauseReason::Count)> pause_reasons_{};

  /** Set by reveal_window(); true once the window has been shown. */
  bool window_shown_{false};

  /** True while a portal scene change is held back for the fade to cover it. */
  bool scene_swap_pending_{false};
};

} // namespace corundum
