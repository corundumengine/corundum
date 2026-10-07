// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Each include below supplies a complete type for an Engine member (or an inline
// symbol the public API uses); do not slim this block.
#include <corundum/audio/audio_system.hpp>
#include <corundum/core/game_config.hpp>
#include <corundum/core/math/vec.hpp>
#include <corundum/core/rng.hpp>
#include <corundum/core/time/loop_timer.hpp>
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
#include <corundum/ui/settings.hpp>
#include <corundum/ui/toast.hpp>
#include <corundum/world/flags.hpp>
#include <corundum/world/scene.hpp>
#include <corundum/world/tilemap/tilemap.hpp>
#include <corundum/world/transition.hpp>
#include <corundum/world/ui_stack.hpp>

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace corundum {

  namespace platform {
    struct PlatformContext;
  } // namespace platform

  namespace input {
    struct InputIntent;
  } // namespace input

  /** @brief Simulation run state.
   *
   *  Running advances the fixed-step simulation. Paused, while any PauseReason holds, keeps it
   *  still and silences audio; run_frame() still polls platform events and renders while paused.
   */
  enum class RunState : std::uint8_t {
    Running,
    Paused,
  };

  /** @brief Why the simulation is paused. Several can hold at once; the engine runs only when none do.
   *
   *  Controller clears on a press, not on movement: moving the mouse or brushing the desk is not the
   *  player choosing to continue on keyboard and mouse. Like every resume, the press that clears it is
   *  consumed and does not act in the game, even when it is the fullscreen key.
   */
  enum class PauseReason : std::uint8_t {
    Focus,      ///< The window lost focus or was minimised; cleared when focus returns.
    Controller, ///< The active gamepad disconnected with no pad left; cleared on reconnect or a keyboard/mouse
                ///< press.
    Game,       ///< Requested by game code (e.g. a pause menu); only game code clears it.
    Count,
  };

  /** @brief Game engine instance owning all system-level resources and game state.
   *
   * Owns systems directly (no virtual dispatch), the Scene (merged entity world +
   * game state), and all game assets. Lifecycle is intrinsic methods:
   *   initialize → run_loop → cleanup
   *
   * make_engine() heap-allocates it: the embedded entity World is ~0.5 MB and must never sit
   * on a stack frame. Members must still never store pointers or references into sibling
   * members.
   *
   * @see initialize  One-time setup before the main loop.
   * @see run_loop    The main loop: input, fixed-step simulation, rendering.
   * @see cleanup     Resource teardown after the main loop.
   */
  // Member order is deliberate — Scene first for 64-byte alignment, window→gpu→renderer for
  // reverse-order teardown, and the rest grouped by subsystem. Reordering for the 64 bytes the
  // check finds would interleave private state and defeat that grouping on a ~0.5 MB heap object.
  // NOLINTNEXTLINE(clang-analyzer-optin.performance.Padding)
  struct Engine {
    // Declared first: Scene's 64-byte alignment packs cleanly at offset 0 and keeps the rest dense.
    world::Scene scene;

    // Declared window → gpu → renderer so reverse-order destruction is
    // renderer → gpu → window: sokol resources are released while the device is
    // still alive, and the device/window outlive the renderer.
    platform::Handle<platform::Window> window;

    platform::Handle<platform::GpuContext> gpu;

    platform::Handle<platform::Renderer> renderer;

    audio::AudioSystem audio;

    input::InputState input_state;

    /// Physical input → Action translation and the live binding table; see input::InputMapper.
    input::InputMapper input_mapper;

    render::RenderState render;

    core::GameConfig cfg;

    sprites::CharacterRegistry characters;

    corundum::world::FlagStore flags;

    core::math::Colour clear_colour{.r = 30, .g = 30, .b = 35, .a = 255};

    debug::HudOverlay hud;

    /** @brief Transient on-screen notifications, rendered bottom-left and aged by the fixed
     *  timestep. Engine quest events enqueue here; game code calls notify() for its own cues. */
    ui::ToastQueue toasts;

    /** @brief Pause-menu selection state; rendered in GameMode::Menu. */
    ui::MenuState menu;

    /** @brief Settings-screen state; rendered in GameMode::Settings. */
    ui::SettingsState settings_screen;

    core::time::LoopTimer timer{static_cast<float>(core::k_default_simulation_fps)};

    /** @brief Deterministic gameplay RNG, seeded at startup. All gameplay randomness must come from
     *  here so a run is reproducible from its seed. */
    core::Rng rng;

    /** @brief Hook called once per fixed step after the registered fixed_step_systems.
     *
     *  Invoked inside the fixed-timestep loop, after every fixed_step_systems entry
     *  (including gameplay's dialogue/event/quest tick) and before entity deletions are
     *  flushed. @p dt is the fixed timestep (timer.target_dt). Entities marked for deletion
     *  here are drained the same frame.
     */
    std::function<void(Engine &, float dt)> on_fixed_update;

    /** @brief Hook called after the engine reacts to OS lifecycle events.
     *
     *  Invoked once per frame in which at least one PlatformEvents field is set,
     *  after focus, controller and quit handling, so game code observes the
     *  resulting run state. Default-empty; existing games are unaffected.
     */
    std::function<void(Engine &, const platform::PlatformEvents &)> on_platform_event;

    /** @brief Hook replacing the pause menu's Quit command.
     *
     *  When set, selecting Quit invokes it instead of request_quit(), so the game can
     *  interpose a confirmation or route the player to a framing screen. Default-empty
     *  preserves the immediate-quit behaviour. The gameplay framework installs it to offer
     *  Quit to Title.
     */
    std::function<void(Engine &)> on_menu_quit;

    /** @brief Hook replacing the pause menu's Save and Load commands.
     *
     *  When set, selecting Save invokes it with @p saving true and Load with @p saving false,
     *  so the game can open a slot browser in the matching mode. Default-empty preserves the
     *  historical behaviour of raising QuickSave / QuickLoad one step later (the framework
     *  installs this hook to open the Save/Load screen).
     */
    std::function<void(Engine &, bool saving)> on_menu_save_load;

    /** @brief Hook consulted before the pause menu opens on Action::Menu.
     *
     *  Receives the current top GameMode; returning true suppresses the menu open. The
     *  gameplay framework installs it so the menu does not open over the Title, Game over,
     *  Loading and Credits screens. Default-empty means the menu never blocks on a mode.
     */
    std::function<bool(world::GameMode)> blocks_pause_menu;

    /** @brief Screen specs keyed by GameMode, plus the ordered render-layer hooks.
     *
     *  Engine-owned screens (Menu, Settings) register here during initialize(); the gameplay
     *  framework registers its own screens and layer hooks when gameplay::Gameplay is constructed
     *  over the engine. Dispatch happens in update_engine_screens() and the layered render
     *  sequence in render_frame().
     */
    ScreenRegistry screens;

    /** @brief Extra per-fixed-step systems, run in registration order before on_fixed_update.
     *
     *  This is the slot for engine-runtime systems and framework systems — never the game's
     *  own hook. on_fixed_update stays the game's single slot. Each entry is a
     *  `void(Engine&, float dt)` callable; the gameplay framework registers exactly one system
     *  (dialogue update, event processing and the quest tick). Registered once, at construction
     *  time, so the std::function storage allocates only then.
     */
    std::vector<std::function<void(Engine &, float)>> fixed_step_systems;

    /** @brief Input hooks consulted by update_engine_screens() before ScreenRegistry dispatch.
     *
     *  An engine/framework system registers here to claim a step for a screen it opens or
     *  switches (the gameplay framework registers the menu hub here). Each entry runs in
     *  registration order and returns true to consume the step; the game's own slots are
     *  unaffected. Registered once, at initialize time, so the std::function storage allocates
     *  only then.
     */
    std::vector<std::function<bool(Engine &, const input::InputIntent &)>> screen_input_hooks;

    /// True while inside an interior reached from the overworld.
    bool entered_from_world{false};

    /** @brief Take ownership of a backend-created platform handle.
     *
     *  The platform::Handle already carries the backend's destruction function, so
     *  these are plain moves. adopt_platform() and the NullPlatform test bundle use
     *  them to satisfy initialize()'s non-null window/renderer precondition.
     */
    void adopt_window(platform::Handle<platform::Window> value) noexcept {
      window = std::move(value);
    }

    void adopt_gpu(platform::Handle<platform::GpuContext> value) noexcept {
      gpu = std::move(value);
    }

    void adopt_renderer(platform::Handle<platform::Renderer> value) noexcept {
      renderer = std::move(value);
    }

    /** @brief Take ownership of a complete platform bundle.
     *
     *  Moves the window, GPU context, renderer and audio backend out of @p platform in the
     *  order initialize() and the main loop expect; @p platform is left empty. Pass a
     *  platform::PlatformContext from create_platform().
     */
    void adopt_platform(platform::PlatformContext platform);

    /** @brief Initialise all systems and load game assets.
     *
     *  @param[in] cfg Fully-loaded game configuration (move-ownership).
     *  @return ok on success, or std::unexpected with an error message.
     *  @pre window and renderer are non-null (make_engine() satisfies this).
     *       gpu may be left null (the main loop never touches it) for backends
     *       that don't need a GPU context.
     *  @post On failure, cleanup() has been run on the partially-initialised
     *        engine; discard it.
     */
    [[nodiscard]] std::expected<void, std::string> initialize(core::GameConfig &&cfg);

    /** @brief Advance the engine by exactly one frame: poll input, run pending
     *  fixed steps, render once with interpolation.
     *
     *  @return true while the loop should continue; false once quit is requested
     *          or the window has closed. run_loop() is equivalent to
     *          while (run_frame()) {}.
     *
     *  @pre initialize() must have returned successfully.
     *  @post If no step-owning screen was on top during a fixed step, both fixed_step_systems
     *        and on_fixed_update ran. If a step-owning screen was on top, neither ran, and
     *        world::update and deletion flushing were skipped — which in turn skips the
     *        gameplay fixed-step system's dialogue update, event processing and quest tick.
     *        This holds whatever the top GameMode is.
     *  @note Allocates only for dialogue-event item/flag bookkeeping and, in World
     *        mode, for the one chunk streamed in per frame.
     */
    [[nodiscard]] bool run_frame() noexcept;

    /** @brief Run the main loop until the window closes or quit is requested.
     *
     *  Equivalent to while (run_frame()) {}.
     *
     *  @pre initialize() must have returned successfully.
     *  @note Allocation behaviour is that of run_frame(), once per iteration.
     */
    void run_loop() noexcept;

    /** @brief Run the main loop followed by orderly teardown.
     *
     *  Equivalent to run_loop() followed by cleanup().
     *
     *  @pre initialize() must have returned successfully.
     */
    void run() noexcept;

    /** @brief Step the top screen through the registry for one fixed step.
     *
     *  Runs the registered screen_input_hooks (which may consume the step by opening or
     *  switching a screen), then handles the pause-menu open input, then dispatches the top
     *  mode's ScreenSpec::update. Returns true only for a registered mode whose spec owns the
     *  step, so the caller skips world::update and the rest of the simulation — which is what
     *  pauses the world while a menu is open.
     *
     *  Exposed for testability; run_frame() calls it once per fixed step through
     *  run_fixed_steps().
     */
    [[nodiscard]] bool update_engine_screens(const input::InputIntent &intent);

    /** @brief Enqueue a transient on-screen notification with the default colour.
     *
     *  Drawn bottom-left and auto-dismissed after ui::k_toast_ttl_seconds. The engine enqueues
     *  quest-start/update/complete/failed messages itself; this is for game-specific cues.
     *
     *  @param text Message to display.
     */
    void notify(std::string text);

    /** @brief Enqueue a transient on-screen notification in @p colour.
     *  @param text   Message to display.
     *  @param colour Text colour; use one of the ui::k_toast_*_colour constants for the standard tints.
     */
    void notify(std::string text, core::math::Colour colour);

    /** @brief Queue @p action to be raised on the next fixed step that no screen owns.
     *
     *  For screen code that must hand an intent to the simulation without mutating
     *  input_state directly (e.g. the pause menu's Save/Load entries raise QuickSave/QuickLoad).
     *  The slot holds one action (last write wins) and lives outside input_state, so
     *  clear_pressed() cannot drop it. update_engine_screens() promotes it into
     *  input_state.pressed at the first step whose top screen does not own the step, keeping it
     *  pending while a step-owning screen (the pause menu, a hub tab, settings) is open. A
     *  lower bound of one step of latency is deliberate: the action is observed by
     *  fixed_step_systems and on_fixed_update on the step after it is raised, never by a
     *  screen's own Activate handling.
     *
     *  @param action Action to raise; ignored when it is not a valid Action index.
     */
    void raise_action_next_step(input::Action action) noexcept;

    /** @brief Request a graceful shutdown.
     *
     *  Sets the quit flag; the next iteration of run_loop() will exit the main
     *  loop. Window closing is handled exclusively by cleanup(). Safe to call from
     *  any system during update().
     */
    void request_quit() noexcept;

    /** @brief Switch the window between windowed and borderless fullscreen.
     *
     *  For an options menu or game code, applied from a safe point (e.g. the title screen), not
     *  mid-gameplay. initialize() applies GameConfig::window_mode directly.
     */
    void toggle_fullscreen() const noexcept;

    /** @brief Tear down resources after the main loop exits.
     *
     *  Shuts down audio and closes the window. Safe to call multiple times; after
     *  the first call, the only valid operation on the Engine is destruction.
     *
     *  @pre run_loop() must have returned, or initialize() has failed.
     */
    void cleanup() noexcept;

    /** @brief The single loaded tilemap, if the engine is in single-map mode.
     *
     *  @return Pointer to the active tilemap, or nullptr in World mode or before load.
     *  @see render::active_tilemap() for the RenderState-level accessor.
     */
    [[nodiscard]] const world::tilemap::Tilemap *active_tilemap() const noexcept {
      return corundum::render::active_tilemap(render);
    }

    /** @brief True once request_quit() or cleanup() has been called. */
    [[nodiscard]] bool quit_requested() const noexcept {
      return quit_;
    }

    /** @brief Current simulation run state: Paused while any PauseReason holds. */
    [[nodiscard]] RunState run_state() const noexcept {
      return is_paused() ? RunState::Paused : RunState::Running;
    }

    /** @brief True while any PauseReason holds. */
    [[nodiscard]] bool is_paused() const noexcept {
      return pause_reasons_.any();
    }

    /** @brief True while @p reason holds. */
    [[nodiscard]] bool is_paused_for(PauseReason reason) const noexcept {
      return pause_reasons_[static_cast<std::size_t>(reason)];
    }

    /** @brief Add @p reason; the first reason pauses the simulation and audio. Idempotent. */
    void pause(PauseReason reason) noexcept;

    /** @brief Remove @p reason. Idempotent.
     *
     *  Removing the last reason resumes. The loop timer's accumulator is cleared so the paused
     *  interval is not replayed as catch-up steps, and presses latched while paused are dropped, so
     *  the click that refocused the window or the key that dismissed a prompt does not also act in
     *  the game.
     */
    void resume(PauseReason reason) noexcept;

    /** @brief Live window width in screen pixels (0 before the first frame). */
    [[nodiscard]] int window_width() const noexcept {
      return window_width_;
    }

    /** @brief Live window height in screen pixels (0 before the first frame). */
    [[nodiscard]] int window_height() const noexcept {
      return window_height_;
    }

  private:
    /** @brief Reveal the window once the first frame is on screen, then apply the configured mode.
     *
     *  The window is created hidden, so the player never watches a blank window while assets load.
     *  GameConfig::window_mode is applied here rather than during initialize() because entering
     *  fullscreen while the window is still hidden leaves it hidden on macOS. Idempotent: only the
     *  first call reaches the platform.
     */
    void reveal_window() noexcept;

    /** @brief Register the engine-owned screen specs.
     *
     *  Called once from initialize(). The gameplay framework registers its own screens,
     *  fixed-step system and layer hooks when gameplay::Gameplay is constructed over the engine. */
    void register_screens();

    bool quit_{false}; ///< Set by request_quit()/cleanup(); see quit_requested().

    /** @brief One-slot action queued by raise_action_next_step(); promoted in update_engine_screens(). */
    std::optional<input::Action> pending_action_{};

    int window_height_{0}; ///< Cached each frame by run_frame(); see window_height().

    int window_width_{0}; ///< Cached each frame by run_frame(); see window_width().

    std::bitset<static_cast<std::size_t>(PauseReason::Count)> pause_reasons_{}; ///< See pause()/resume().

    bool window_shown_{false}; ///< Set by reveal_window(); true once the window has been shown.
  };

} // namespace corundum
