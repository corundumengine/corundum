// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/game_config.hpp>
#include <corundum/core/math/isometric.hpp>
#include <corundum/core/math/vec.hpp>
#include <corundum/core/window_mode.hpp>
#include <corundum/debug/debug_overlay.hpp>
#include <corundum/engine.hpp>
#include <corundum/entities/world.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/bindings.hpp>
#include <corundum/input/input_intent.hpp>
#include <corundum/input/input_system.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/platform/platform_events.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/platform/window.hpp>
#include <corundum/render/render_state.hpp>
#include <corundum/render/render_system.hpp>
#include <corundum/screen_registry.hpp>
#include <corundum/settings/user_settings.hpp>
#include <corundum/ui/menu.hpp>
#include <corundum/ui/prompt_box.hpp>
#include <corundum/ui/settings.hpp>
#include <corundum/ui/toast.hpp>
#include <corundum/world/map_view.hpp>
#include <corundum/world/portals/portal.hpp>
#include <corundum/world/spawn.hpp>
#include <corundum/world/transition.hpp>
#include <corundum/world/ui_stack.hpp>
#include <corundum/world/update.hpp>
#include <corundum/world/world_bounds.hpp>

#include "core/warn_log.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace corundum {

  namespace {

    using corundum::detail::warn_log;

    /// Owns the initialize() sequence as private, ordered phases. The only
    /// public entry point is run() — phase order is therefore structural
    /// (nothing outside this class can call a phase out of order), not a
    /// convention enforced by statement order in one function.
    class InitPipeline {
    public:
      explicit InitPipeline(Engine &engine) : engine_(&engine) {}

      std::expected<void, std::string> run(core::GameConfig &&cfg) {
        engine_->cfg = std::move(cfg);
        engine_->timer.set_target_fps(static_cast<float>(engine_->cfg.simulation_fps));
        engine_->window->set_vsync(engine_->cfg.vsync);

        const std::uint64_t rng_seed = engine_->cfg.rng_seed.value_or([] {
          std::random_device rd;
          return (static_cast<std::uint64_t>(rd()) << 32U) | static_cast<std::uint64_t>(rd());
        }());
        engine_->rng = core::Rng{rng_seed};
        corundum::detail::info_log("[engine] RNG seed {}", rng_seed);

        if (auto result = load_render_assets(); !result)
          return result;

        if (auto result = init_scene(); !result)
          return result;

        init_audio();
        render::configure_panel_style(engine_->render, engine_->cfg);
        return {};
      }

    private:
      std::expected<void, std::string> load_render_assets() {
        std::expected<void, std::string> characters_result;
        characters_result = engine_->characters.load_all(engine_->cfg.paths.sprites_dir);
        if (!characters_result)
          return std::unexpected(characters_result.error());
        render::load_sprite_index(*engine_->renderer, engine_->render, engine_->characters);

        const auto font_path = std::format("{}/{}", engine_->cfg.paths.font_dir, engine_->cfg.paths.game_font);
        std::expected<uint32_t, std::string> font_result;
        font_result = render::load_font(*engine_->renderer, engine_->render, font_path);
        if (!font_result)
          return std::unexpected(font_result.error());

        std::expected<void, std::string> ui_result;
        ui_result = render::load_ui_assets(*engine_->renderer, engine_->render);
        if (!ui_result)
          return std::unexpected(ui_result.error());
        return {};
      }

      std::expected<void, std::string> init_scene() {
        if (!engine_->cfg.paths.world_manifest_path.empty())
          return corundum::world::enter_world(*engine_, {});
        return init_single_map_scene();
      }

      std::expected<void, std::string> init_single_map_scene() {
        std::expected<void, std::string> map_result;
        map_result =
            render::load_map(*engine_->renderer, engine_->render, engine_->cfg.paths.tilemap_path, engine_->cfg);
        if (!map_result)
          return std::unexpected(std::move(map_result).error());

        std::expected<std::unique_ptr<world::Scene>, std::string> scene_result =
            world::spawn_world(engine_->cfg, engine_->characters, *engine_->active_tilemap());
        if (!scene_result)
          return std::unexpected(std::move(scene_result).error());
        engine_->scene = std::move(**scene_result);

        const auto &tilemap = *engine_->active_tilemap();
        const auto iso = core::math::compute_isometric_params(tilemap.diamond_w(), tilemap.diamond_h(), tilemap.height,
                                                              engine_->cfg.tile_scale, engine_->cfg.elevation_step_px);
        const std::uint32_t player_slot = engine_->scene.world.transforms.dense_index(engine_->scene.player);
        const float player_col{engine_->scene.world.transforms.col[player_slot]};
        const float player_row{engine_->scene.world.transforms.row[player_slot]};

        const world::WorldBounds bounds{world::single_map_bounds(tilemap, iso.half_tw, iso.half_th)};

        world::frame_camera_on(*engine_, iso, player_col, player_row, bounds, world::CameraAnchor::TopVertex);
        return {};
      }

      void init_audio() {
        std::expected<void, std::string> audio_result;
        audio_result = engine_->audio.initialize(engine_->cfg.paths.sounds_dir);
        if (!audio_result) {
          corundum::detail::warn_log("[engine] WARN: Audio init failed — {}", audio_result.error());
          return;
        }
        engine_->audio.load_catalog(engine_->cfg.paths.sounds_catalog);
      }

      Engine *engine_;
    };

  } // namespace

  std::expected<void, std::string> Engine::initialize(core::GameConfig &&cfg) {
    if (!window || !renderer)
      return std::unexpected("Engine::initialize: window and renderer must be non-null "
                             "(use make_engine(), or adopt a platform before calling)");

    InitPipeline pipeline(*this);
    if (auto result = pipeline.run(std::move(cfg)); !result) {
      cleanup();
      return std::unexpected(result.error());
    }
    register_screens();
    return {};
  }

  namespace {

    /// Result of one frame's fixed-step simulation.
    struct SimulationResult {
      int steps_run{0};
      /// True when the drain exhausted its step budget and dropped queued simulation time.
      bool budget_exhausted{false};
    };

    /// Invoke the user-provided platform-event hook, swallowing any exceptions
    /// so the frame's noexcept contract holds.
    void invoke_platform_event_hook(Engine &engine, const platform::PlatformEvents &events) noexcept {
      if (!engine.on_platform_event)
        return;
      try {
        engine.on_platform_event(engine, events);
      } catch (...) {
        warn_log("[engine] WARN: on_platform_event handler threw");
      }
    }

    /// Pause when the player loses their only controller mid-play; resume once a pad is back or
    /// they have pressed something on the keyboard or mouse instead.
    void update_controller_pause(Engine &engine, const platform::PlatformEvents &events) noexcept {
      const input::InputMapper &mapper{engine.input_mapper};
      const bool playing_on_gamepad{mapper.last_device() == input::InputDevice::Gamepad};
      if (events.controller_disconnected && playing_on_gamepad && !mapper.gamepad_connected())
        engine.pause(PauseReason::Controller);
      else if (engine.is_paused_for(PauseReason::Controller) && (mapper.gamepad_connected() || !playing_on_gamepad))
        engine.resume(PauseReason::Controller);
    }

    /// Poll platform input and OS lifecycle events, apply focus, controller and quit behaviour,
    /// then hand the frame to the on_platform_event hook.
    void process_input(Engine &engine) noexcept {
      platform::PlatformEvents events{};
      input::poll(engine.input_mapper, engine.input_state, *engine.window, events);

      if (events.focus_lost)
        engine.pause(PauseReason::Focus);
      // Handled after focus_lost so a same-frame loss-and-gain nets to running.
      if (events.focus_gained)
        engine.resume(PauseReason::Focus);
      if (events.quit_requested)
        engine.request_quit();
      update_controller_pause(engine, events);

      if (platform::has_any_event(events))
        invoke_platform_event_hook(engine, events);

      if (engine.input_state.is_held(input::Action::Quit))
        engine.request_quit();
    }

    /// Invoke the user-provided per-fixed-step hook, swallowing any exceptions
    /// so the noexcept contract on the simulation loop holds.
    void invoke_fixed_update_hook(Engine &engine, float dt) noexcept {
      if (!engine.on_fixed_update)
        return;
      try {
        engine.on_fixed_update(engine, dt);
      } catch (...) {
        warn_log("[engine] WARN: on_fixed_update handler threw");
      }
    }

    /// Modulo wrap of a UI list cursor, matching the dialogue choice cursor: Down past the last
    /// row lands on the first, Up past the first lands on the last. @p count must be > 0.
    int wrap_cursor(int current, int delta, int count) noexcept {
      return (current + delta + count) % count;
    }

    /// Live values the Settings screen shows, read from the engine each frame it renders.
    ui::SettingsValues settings_values(const Engine &engine) {
      return ui::SettingsValues{
          .master_volume = engine.audio.master_volume(),
          .text_speed = engine.render.text_speed,
          .ui_scale = engine.render.ui_scale,
          .window_mode = engine.window->window_mode(),
      };
    }

    /// Re-derive the dialogue style after an edit to ui_scale or text_speed.
    void refresh_dialog_style(Engine &engine) {
      render::configure_panel_style(engine.render, engine.cfg);
    }

    /// True when @p input is the conventional Back press during a rebind capture. Capture
    /// swallows every press before it reaches the action bitsets, so Back must be recognised
    /// from the captured physical input itself.
    bool is_cancel_capture(input::PhysicalInput input) noexcept {
      return (input.device == input::InputDevice::Keyboard &&
              input.code == static_cast<std::uint16_t>(input::Key::Escape)) ||
             (input.device == input::InputDevice::Gamepad &&
              input.code == static_cast<std::uint16_t>(input::GamepadControl::B));
    }

    /// Apply the change for a Left/Right press on a General-tab row.
    void adjust_general_row(Engine &engine, ui::SettingsGeneralRow row, int direction) {
      switch (row) {
        case ui::SettingsGeneralRow::Volume:
          engine.audio.set_master_volume(
              std::clamp(engine.audio.master_volume() + (static_cast<float>(direction) * ui::k_master_volume_step), 0.f,
                         1.f));
          break;
        case ui::SettingsGeneralRow::TextSpeed:
          engine.render.text_speed = direction > 0 ? ui::next_text_speed(engine.render.text_speed)
                                                   : ui::prev_text_speed(engine.render.text_speed);
          refresh_dialog_style(engine);
          break;
        case ui::SettingsGeneralRow::UiScale:
          engine.render.ui_scale =
              std::clamp(engine.render.ui_scale + (static_cast<float>(direction) * ui::k_ui_scale_step),
                         ui::k_ui_scale_min, ui::k_ui_scale_max);
          refresh_dialog_style(engine);
          break;
        case ui::SettingsGeneralRow::WindowMode:
        case ui::SettingsGeneralRow::Count:
          break; // Window mode toggles on Activate, not on Left/Right.
      }
    }

    /// Bind @p captured to the action under the Controls cursor, replacing that action's existing
    /// binding on the same device class (so a keyboard rebind does not add a second key row).
    void rebind_captured(Engine &engine, input::PhysicalInput captured) {
      const auto action = static_cast<input::Action>(engine.settings_screen.cursor);
      input::Bindings bindings = engine.input_mapper.bindings();
      for (const input::PhysicalInput existing : input::inputs_for(bindings, action)) {
        if (existing.device == captured.device)
          input::unbind(bindings, action, existing);
      }
      input::bind(bindings, action, captured);
      if (auto result = engine.input_mapper.set_bindings(std::move(bindings)); !result)
        warn_log("[engine] WARN: rebind failed: {}", result.error());
    }

    /// Step the pause menu: Back/Menu closes it; Up/Down move the selection; Activate runs the
    /// highlighted command.
    void update_pause_menu(Engine &engine, const input::InputIntent &intent) {
      if (intent.back || engine.input_state.is_pressed(input::Action::Menu)) {
        engine.scene.ui.pop();
        return;
      }
      if (intent.navigate_y != 0)
        engine.menu.cursor = wrap_cursor(engine.menu.cursor, intent.navigate_y, ui::k_menu_command_count);
      if (!intent.activate)
        return;

      switch (ui::menu_command_at(engine.menu.cursor)) {
        case ui::MenuCommand::Resume:
          engine.scene.ui.pop();
          break;
        case ui::MenuCommand::Settings:
          engine.settings_screen = {};
          engine.scene.ui.push(world::GameMode::Settings);
          break;
        case ui::MenuCommand::Quit:
          engine.request_quit();
          break;
      }
    }

    /// Step the Settings screen. While the Controls tab is capturing a rebind, every other
    /// intent is ignored; otherwise Back returns to the menu, TabNext/Prev switch pages, and the
    /// cursor edits the focused row.
    void update_settings(Engine &engine, const input::InputIntent &intent) {
      ui::SettingsState &state = engine.settings_screen;

      if (state.rebinding) {
        if (const std::optional<input::PhysicalInput> captured = engine.input_mapper.take_captured()) {
          if (is_cancel_capture(*captured))
            engine.input_mapper.cancel_capture();
          else
            rebind_captured(engine, *captured);
          state.rebinding = false;
        }
        return;
      }

      if (intent.back || engine.input_state.is_pressed(input::Action::Menu)) {
        engine.scene.ui.pop();
        return;
      }

      if (intent.next_tab || intent.prev_tab) {
        const int direction = intent.next_tab ? 1 : -1;
        const int tabs = ui::k_settings_tab_count;
        state.tab = static_cast<ui::SettingsTab>((static_cast<int>(state.tab) + direction + tabs) % tabs);
        state.cursor = 0;
        state.scroll = 0;
        return;
      }

      const int row_count = ui::settings_row_count(state.tab);
      const int visible_rows = std::min(row_count, ui::k_settings_max_visible_rows);
      if (intent.navigate_y != 0) {
        state.cursor = wrap_cursor(state.cursor, intent.navigate_y, row_count);
        ui::settings_scroll_to_cursor(state, row_count, visible_rows);
        return;
      }

      if (state.tab == ui::SettingsTab::General) {
        const auto row = static_cast<ui::SettingsGeneralRow>(state.cursor);
        if (intent.navigate_x != 0) {
          adjust_general_row(engine, row, intent.navigate_x);
        } else if (intent.activate && row == ui::SettingsGeneralRow::WindowMode) {
          const bool fullscreen = engine.window->window_mode() == core::WindowMode::Fullscreen;
          engine.window->set_window_mode(fullscreen ? core::WindowMode::Windowed : core::WindowMode::Fullscreen);
        }
      } else if (intent.activate) {
        engine.input_mapper.begin_capture();
        state.rebinding = true;
      }
    }

    /// Upper bound on catch-up work per frame. At 60 Hz this is ~133 ms of catch-up; past it the
    /// simulation sheds the remaining time rather than compounding a backlog.
    constexpr int k_max_steps_per_frame{8};

    /// Drain the timer accumulator: run the world step, the registered fixed-step systems and the
    /// on_fixed_update hook, and flush deletions once per fixed step.
    ///
    /// Simulation work is intentionally not wrapped — a throw here (allocation failure in an
    /// engine UI screen) is fatal, not recoverable.
    // NOLINTNEXTLINE(bugprone-exception-escape)
    [[nodiscard]] SimulationResult run_fixed_steps(Engine &engine) noexcept {
      const float pending_time{engine.timer.accumulator};
      const int steps{engine.timer.take_steps(k_max_steps_per_frame)};
      const float consumed{static_cast<float>(steps) * engine.timer.target_dt};
      // take_steps() sheds whatever remains once it hits the cap; that remainder is dropped time.
      SimulationResult result{
          .steps_run = steps,
          .budget_exhausted = steps == k_max_steps_per_frame && pending_time > consumed,
      };

      for (int step_index = 0; step_index < steps; ++step_index) {
        render::snapshot_previous_step(engine.render, engine.scene);

        const input::InputIntent intent =
            input::make_input_intent(engine.input_state, engine.input_mapper.last_device());
        // Screen input hooks (the gameplay menu hub) and the engine-owned Menu/Settings screens
        // own the step while open, which is what pauses the simulation — no world update,
        // gameplay systems, or on_fixed_update while a screen is up. Call it unconditionally: it
        // has side effects (opening a screen) even when no screen ends up active. Opening the
        // pause menu happens here because Esc doubles as Cancel — opening consumes the step so
        // the same press cannot also close the fresh menu.
        const bool engine_screen_active = engine.update_engine_screens(intent);

        // Transient UI keeps aging while a screen pauses the simulation, so a toast raised just
        // before opening a screen still expires instead of freezing on display.
        engine.toasts.update(engine.timer.target_dt);

        if (!engine_screen_active) {
          engine.scene.elapsed_time += engine.timer.target_dt;

          // World mode with nothing streamed in has no actors to simulate, but time still advances
          // and the input edge must still be consumed: poll() only ORs presses in, so a press
          // latched here would fire the moment the world activates.
          if (engine.render.mode != render::RenderMode::World || !engine.render.chunks.active_empty()) {
            const world::MapView map_view = world::build_map_view(engine.render, engine.cfg);
            world::sync_chunk_actors(engine.scene, engine.render, engine.cfg, engine.characters);
            // Core simulation is intentionally not wrapped: unlike the game seams (dialogue
            // dispatch, on_fixed_update, map transitions), a throw here is fatal, not recoverable.
            world::update(engine.scene, engine.cfg, engine.input_state, map_view, engine.timer.target_dt,
                          static_cast<float>(engine.window_width()), static_cast<float>(engine.window_height()),
                          engine.input_mapper.last_device());
          }

          // Registered systems (gameplay's dialogue/event/quest tick) run every step — including
          // World mode with nothing streamed in — so queued work is never stranded while
          // elapsed_time advances. They run after world::update and before on_fixed_update.
          for (const std::function<void(Engine &, float)> &system : engine.fixed_step_systems)
            system(engine, engine.timer.target_dt);

          invoke_fixed_update_hook(engine, engine.timer.target_dt);

          entities::flush_deletions(engine.scene.world);
        }

        input::clear_pressed(engine.input_state);
      }

      return result;
    }

    void render_menu(const Engine &engine, platform::Renderer &r, core::math::Vec2 viewport) {
      ui::menu_panel_render(r, engine.render.panel_skin.style, engine.render.panel_skin.border, engine.menu, viewport,
                            engine.input_mapper.last_device());
    }

    void render_settings(const Engine &engine, platform::Renderer &r, core::math::Vec2 viewport) {
      ui::settings_panel_render(r, engine.render.panel_skin.style, engine.render.panel_skin.border, engine.settings_screen,
                                settings_values(engine), engine.input_mapper.bindings(), viewport,
                                engine.input_mapper.last_device());
    }

    /// begin_frame → world → layered overlays (HUD, toasts, modals, screens, hub) → debug HUD →
    /// end_frame. The overlay order is fixed; each registered hook gates on its own state.
    // std::format with a literal format string cannot throw at run time, and a throw from the
    // frame path is intentionally fatal (noexcept).
    // NOLINTNEXTLINE(bugprone-exception-escape)
    void render_frame(Engine &engine, const float alpha, const bool budget_exhausted) noexcept {
      if (!engine.renderer->begin_frame(engine.clear_colour))
        return;

      platform::Renderer &r = *engine.renderer;
      render::render(r, engine.render, engine.cfg, engine.scene, alpha, engine.window_width(),
                     engine.window_height());

      const corundum::core::math::Vec2 viewport{
          .x = static_cast<float>(engine.window_width()),
          .y = static_cast<float>(engine.window_height()),
      };
      const bool screen_open = !engine.scene.ui.empty();
      const bool transition_showing =
          engine.scene.transition_prompt && !engine.scene.transition_prompt->declined();

      // 2. Gameplay HUD, only while nothing is modal.
      if (!screen_open && !transition_showing)
        engine.screens.render_layer(RenderLayer::Hud, engine, r, viewport);

      // 3. Engine toasts.
      engine.toasts.render(r, engine.render.panel_skin.style, viewport);

      // 4a. Engine-owned transition prompt, drawn whenever pending regardless of mode.
      if (transition_showing) {
        const std::string_view question =
            engine.scene.transition_prompt->transition().return_to_world ? "Leave?" : "Enter?";
        ui::prompt_box_render(r, engine.render.panel_skin.style, engine.render.panel_skin.border, question,
                              engine.scene.transition_prompt->confirm_selected(), viewport);
      }

      // 4b. Modal hooks, each gated on its own state and on being the top-of-stack mode
      // (the dialogue box hides while a screen is pushed over it).
      engine.screens.render_layer(RenderLayer::Modal, engine, r, viewport);

      // 4c. The top mode's own screen, if it registered a render hook.
      if (const ScreenSpec *spec = engine.screens.find(engine.scene.mode()); spec != nullptr && spec->render)
        spec->render(engine, r, viewport);

      // 5. Hub panels, then the hub tab strip.
      engine.screens.render_layer(RenderLayer::HubPanel, engine, r, viewport);
      engine.screens.render_layer(RenderLayer::HubStrip, engine, r, viewport);

      // 6. Debug HUD, always last.
      const debug::OverlayInput hud_input{
          .render_state = &engine.render,
          .cfg = &engine.cfg,
          .scene = &engine.scene,
          .timer = &engine.timer,
          .viewport = viewport,
          .step_budget_exhausted = budget_exhausted,
      };
      engine.hud.render(r, hud_input);

      engine.renderer->end_frame();
    }

  } // namespace

  void Engine::register_screens() {
    // Engine-owned screens. They capture nothing and read the Engine& argument.
    screens.add(world::GameMode::Menu,
                ScreenSpec{
                    .owns_step = true,
                    .update = [](Engine &engine, const input::InputIntent &intent) { update_pause_menu(engine, intent); },
                    .render = [](Engine &engine, platform::Renderer &r, core::math::Vec2 viewport) {
                      render_menu(engine, r, viewport);
                    },
                });
    screens.add(world::GameMode::Settings,
                ScreenSpec{
                    .owns_step = true,
                    .update = [](Engine &engine, const input::InputIntent &intent) { update_settings(engine, intent); },
                    .render = [](Engine &engine, platform::Renderer &r, core::math::Vec2 viewport) {
                      render_settings(engine, r, viewport);
                    },
                });
  }

  bool Engine::update_engine_screens(const input::InputIntent &intent) {
    // Registered input hooks (the gameplay menu hub) get first refusal and may consume the step
    // by opening, closing or switching a screen.
    for (const std::function<bool(Engine &, const input::InputIntent &)> &hook : screen_input_hooks) {
      if (hook(*this, intent))
        return true;
    }

    // The pause menu opens over any screen: Resume pops the single layer it pushed and reveals
    // whatever was beneath (a hub tab, dialogue, loot or barter). One physical press may raise
    // both Menu and Cancel (Esc), so with a screen open the press is left to that screen's own
    // back handling rather than opening the menu and closing it again the same step; from an
    // empty stack there is nothing to close and both are true, so the menu opens. Menu and
    // Settings consume Menu themselves (to close, or to capture a rebind), so the menu never
    // opens over either.
    const bool menu_pressed = input_state.is_pressed(input::Action::Menu);
    const bool top_handles_menu = scene.mode() == world::GameMode::Menu || scene.mode() == world::GameMode::Settings;
    if (menu_pressed && !top_handles_menu && (scene.ui.empty() || !intent.back)) {
      scene.ui.push(world::GameMode::Menu);
      menu.cursor = 0;
      return true;
    }

    // Dispatch the top mode's screen. Only a step-owning screen reports the step as taken;
    // a non-step-owning extension mode (Dialogue) leaves the world step running.
    const ScreenSpec *spec = screens.find(scene.mode());
    if (spec == nullptr || !spec->owns_step)
      return false;
    if (spec->update)
      spec->update(*this, intent);
    return true;
  }

  void Engine::run_loop() noexcept {
    // Measure the first frame from the start of the loop, so asset-load time during initialize() is not
    // replayed as a burst of catch-up fixed steps.
    timer.prev_time = std::chrono::steady_clock::now();
    while (run_frame()) {
    }
  }

  void Engine::run() noexcept {
    run_loop();
    cleanup();
  }

  bool Engine::run_frame() noexcept {
    if (!window->is_open() || quit_)
      return false;
    std::tie(window_width_, window_height_) = window->size();

    process_input(*this);
    if (quit_)
      return false;

    // While paused the timer is deliberately not ticked: wall time spent paused must not
    // queue catch-up fixed steps. Rendering still runs so a pause menu stays on screen.
    if (is_paused()) {
      render_frame(*this, timer.alpha(), /*budget_exhausted=*/false);
      reveal_window();
      return true;
    }

    timer.tick();

    render::stream_world_chunks(*renderer, render, cfg, scene);

    const SimulationResult simulation{run_fixed_steps(*this)};
    // A transition re-snapshots through frame_camera_on, so the blend never spans two scenes.
    world::handle_map_transition(*this);
    render_frame(*this, timer.alpha(), simulation.budget_exhausted);
    reveal_window();

    return true;
  }

  void Engine::reveal_window() noexcept {
    if (window_shown_)
      return;
    window_shown_ = true;
    window->show();
    // Enter the configured mode only after the window is visible: glfwSetWindowMonitor on a hidden
    // window leaves it permanently hidden on macOS, and a later show() cannot recover it.
    window->set_window_mode(cfg.window_mode);
  }

  void Engine::pause(PauseReason reason) noexcept {
    const bool was_paused{is_paused()};
    pause_reasons_[static_cast<std::size_t>(reason)] = true;
    if (!was_paused)
      audio.set_paused(true);
  }

  void Engine::resume(PauseReason reason) noexcept {
    const bool was_paused{is_paused()};
    pause_reasons_[static_cast<std::size_t>(reason)] = false;
    if (!was_paused || is_paused())
      return;

    audio.set_paused(false);
    timer.accumulator = 0.f;
    timer.prev_time = std::chrono::steady_clock::now();
    input::clear_pressed(input_state);
  }

  void Engine::cleanup() noexcept {
    audio.shutdown();
    if (window)
      window->close();
    quit_ = true;
  }

  void Engine::request_quit() noexcept {
    quit_ = true;
  }

  void Engine::notify(std::string text) {
    toasts.notify(std::move(text));
  }

  void Engine::notify(std::string text, core::math::Colour colour) {
    toasts.notify(std::move(text), colour);
  }

  void Engine::toggle_fullscreen() const noexcept {
    const bool fullscreen{window->window_mode() == core::WindowMode::Fullscreen};
    window->set_window_mode(fullscreen ? core::WindowMode::Windowed : core::WindowMode::Fullscreen);
  }

} // namespace corundum
