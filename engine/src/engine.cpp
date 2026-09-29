// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/game_config.hpp>
#include <corundum/core/math/isometric.hpp>
#include <corundum/core/window_mode.hpp>
#include <corundum/debug/debug_overlay.hpp>
#include <corundum/dialogue/action.hpp>
#include <corundum/dialogue/validate_refs.hpp>
#include <corundum/engine.hpp>
#include <corundum/entities/world.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/input_system.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/item/item.hpp>
#include <corundum/platform/platform_events.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/platform/window.hpp>
#include <corundum/quest/runner.hpp>
#include <corundum/quest/system.hpp>
#include <corundum/render/render_state.hpp>
#include <corundum/render/render_system.hpp>
#include <corundum/world/map_view.hpp>
#include <corundum/world/spawn.hpp>
#include <corundum/world/transition.hpp>
#include <corundum/world/update.hpp>
#include <corundum/world/world_bounds.hpp>

#include "core/warn_log.hpp"

#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>

namespace corundum {

  namespace {

    /// Parse args[index] as an int; returns `fallback` if absent, unparseable, or only
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

    using corundum::detail::warn_log;

    void validate_quest_references(const corundum::dialogue::Registry &graphs, const corundum::quest::Registry &quests,
                                   const corundum::item::Registry &items) {
      for (const auto &[id, graph] : graphs) {
        for (const auto &err : corundum::dialogue::validate_quest_refs(graph, quests, &items, &graphs))
          warn_log("[engine] WARN: dialogue '{}' {}", id, err);
        for (const auto &err : corundum::dialogue::validate_condition_quest_refs(graph, quests))
          warn_log("[engine] WARN: dialogue '{}' {}", id, err);
      }
    }

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
        render::configure_dialog_style(engine_->render, engine_->cfg);
        load_dialogue_and_quests();
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

      void load_dialogue_and_quests() {
        int dialogue_loaded{0};
        if (!engine_->cfg.paths.dialogue_dir.empty())
          dialogue_loaded = engine_->graphs.load_all(engine_->cfg.paths.dialogue_dir);
        corundum::detail::info_log("[engine] Loaded {} dialogue graphs from '{}'", dialogue_loaded, engine_->cfg.paths.dialogue_dir);

        int quest_loaded{0};
        if (!engine_->cfg.paths.quests_dir.empty())
          quest_loaded = engine_->quests.load_all(engine_->cfg.paths.quests_dir);
        corundum::detail::info_log("[engine] Loaded {} quests from '{}'", quest_loaded, engine_->cfg.paths.quests_dir);

        int item_loaded{0};
        if (!engine_->cfg.paths.items_dir.empty())
          item_loaded = engine_->items.load_all(engine_->cfg.paths.items_dir);
        corundum::detail::info_log("[engine] Loaded {} items from '{}'", item_loaded, engine_->cfg.paths.items_dir);

        validate_quest_references(engine_->graphs, engine_->quests, engine_->items);
      }

      Engine *engine_;
    };

    /// Outcome of consulting the user-provided dialogue-event hook.
    enum class EventHookResult : std::uint8_t {
      NotHandled, ///< No hook is installed, or it reported the event as unhandled.
      Handled,    ///< The hook reported the event as handled.
      Threw,      ///< The hook threw; already logged, and the event is left suppressed.
    };

    /// Invoke the user-provided dialogue-event hook, swallowing any exceptions
    /// so the noexcept contract on the dispatch loop holds. A throwing hook maps
    /// to Threw so dispatch does not also report the event as unknown.
    EventHookResult invoke_event_hook(Engine &engine, const dialogue::EventAction &ev) noexcept {
      if (!engine.on_event)
        return EventHookResult::NotHandled;
      try {
        return engine.on_event(engine, ev) ? EventHookResult::Handled : EventHookResult::NotHandled;
      } catch (...) {
        warn_log("[engine] WARN: on_event handler threw on '{}'", ev.name);
        return EventHookResult::Threw;
      }
    }

    void handle_play_sound(Engine &engine, const dialogue::EventAction &ev) {
      const auto result = engine.audio.play_sound(ev.args[0]);
      if (!result)
        warn_log("[engine] WARN: {}", result.error());
    }

    void handle_quest_start(quest::Runner &quest_runner, const dialogue::EventAction &ev) {
      if (auto result = quest_runner.start(ev.args[0]); !result)
        warn_log("[engine] WARN: {}", result.error());
    }

    void handle_quest_advance(quest::Runner &quest_runner, const dialogue::EventAction &ev) {
      if (auto result = quest_runner.advance(ev.args[0], ev.args[1]); !result)
        warn_log("[engine] WARN: {}", result.error());
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

    void dispatch_dialogue_event(Engine &engine, quest::Runner &quest_runner,
                                 const dialogue::EventAction &ev) noexcept {
      try {
        if (ev.name == "play_sound" && !ev.args.empty()) {
          handle_play_sound(engine, ev);
        } else if (ev.name == "quest_start" && !ev.args.empty()) {
          handle_quest_start(quest_runner, ev);
        } else if (ev.name == "quest_advance" && ev.args.size() >= 2) {
          handle_quest_advance(quest_runner, ev);
        } else if (ev.name == "give_item" && !ev.args.empty()) {
          engine.flags[item_flag_key(ev.args[0])] += event_int_arg(ev, 1, /*fallback=*/1);
        } else if (ev.name == "take_item" && !ev.args.empty()) {
          handle_take_item(engine, ev);
        } else if (ev.name == "reputation" && ev.args.size() >= 2) {
          // A non-numeric value parses to 0; skip the write so no zero-valued rep flag is created.
          if (const int delta = event_int_arg(ev, 1, /*fallback=*/0); delta != 0)
            engine.flags["rep." + ev.args[0]] += delta;
        } else if (invoke_event_hook(engine, ev) == EventHookResult::NotHandled) {
          warn_log("[engine] WARN: unknown dialogue event '{}'", ev.name);
        }
      } catch (...) {
        // Skip events whose processing throws (e.g. allocation failure); preserves
        // the noexcept contract of process_dialogue_events.
        return;
      }
    }

  } // namespace

  void Engine::process_dialogue_events() noexcept {
    quest::Runner quest_runner{quests, flags};
    for (const auto &ev : scene.pending_dialogue_events)
      dispatch_dialogue_event(*this, quest_runner, ev);
    scene.pending_dialogue_events.clear();
  }

  std::expected<void, std::string> Engine::initialize(core::GameConfig &&cfg) {
    if (!window || !renderer)
      return std::unexpected("Engine::initialize: window and renderer must be non-null "
                             "(use make_engine(), or adopt a platform before calling)");

    InitPipeline pipeline(*this);
    if (auto result = pipeline.run(std::move(cfg)); !result) {
      cleanup();
      return std::unexpected(result.error());
    }
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

    /// Upper bound on catch-up work per frame. At 60 Hz this is ~133 ms of catch-up; past it the
    /// simulation sheds the remaining time rather than compounding a backlog.
    constexpr int k_max_steps_per_frame{8};

    /// Drain the timer accumulator: run gameplay, dialogue events, the
    /// on_fixed_update hook, and deletion flushing once per fixed step.
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
        engine.scene.elapsed_time += engine.timer.target_dt;

        // World mode with nothing streamed in has no actors to simulate, but time still advances
        // and the input edge must still be consumed: poll() only ORs presses in, so a press
        // latched here would fire the moment the world activates.
        if (engine.render.mode != render::RenderMode::World || !engine.render.chunks.active_empty()) {
          const world::MapView map_view = world::build_map_view(engine.render, engine.cfg);
          world::sync_chunk_actors(engine.scene, engine.render, engine.cfg, engine.characters);
          // Core simulation is intentionally not wrapped: unlike the game seams (dialogue
          // dispatch, on_fixed_update, map transitions), a throw here is fatal, not recoverable.
          world::update(engine.scene, engine.cfg, engine.graphs, engine.input_state, map_view, engine.timer.target_dt,
                        static_cast<float>(engine.window_width()), static_cast<float>(engine.window_height()),
                        engine.flags, &engine.quests);
        }

        // Dialogue, quests, and the hook run every step — including World mode with nothing
        // streamed in — so queued work is never stranded while elapsed_time advances.
        engine.process_dialogue_events();
        quest::tick_quests(engine.quests, engine.flags, engine.scene.zone_id);
        invoke_fixed_update_hook(engine, engine.timer.target_dt);

        entities::flush_deletions(engine.scene.world);

        input::clear_pressed(engine.input_state);
      }

      return result;
    }

    /// begin_frame → world/UI render → optional debug HUD → end_frame.
    void render_frame(Engine &engine, const float alpha, const bool budget_exhausted) noexcept {
      if (!engine.renderer->begin_frame(engine.clear_colour))
        return;
      render::render(*engine.renderer, engine.render, engine.cfg, engine.scene, engine.flags, &engine.items, alpha,
                     engine.window_width(), engine.window_height());

      const debug::OverlayInput hud_input{
          .render_state = &engine.render,
          .cfg = &engine.cfg,
          .scene = &engine.scene,
          .timer = &engine.timer,
          .viewport = {.x = static_cast<float>(engine.window_width()),
                       .y = static_cast<float>(engine.window_height()),},
          .step_budget_exhausted = budget_exhausted,
      };
      engine.hud.render(*engine.renderer, hud_input);

      engine.renderer->end_frame();
    }

  } // namespace

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

  void Engine::toggle_fullscreen() const noexcept {
    const bool fullscreen{window->window_mode() == core::WindowMode::Fullscreen};
    window->set_window_mode(fullscreen ? core::WindowMode::Windowed : core::WindowMode::Fullscreen);
  }

} // namespace corundum
