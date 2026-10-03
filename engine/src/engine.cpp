// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/codex/loader.hpp>
#include <corundum/codex/registry.hpp>
#include <corundum/core/game_config.hpp>
#include <corundum/core/math/isometric.hpp>
#include <corundum/core/math/vec.hpp>
#include <corundum/core/window_mode.hpp>
#include <corundum/debug/debug_overlay.hpp>
#include <corundum/dialogue/action.hpp>
#include <corundum/dialogue/validate_refs.hpp>
#include <corundum/engine.hpp>
#include <corundum/entities/world.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/bindings.hpp>
#include <corundum/input/input_intent.hpp>
#include <corundum/input/input_system.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/item/container.hpp>
#include <corundum/item/item.hpp>
#include <corundum/location/loader.hpp>
#include <corundum/location/registry.hpp>
#include <corundum/platform/platform_events.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/platform/window.hpp>
#include <corundum/quest/quest.hpp>
#include <corundum/quest/runner.hpp>
#include <corundum/quest/status.hpp>
#include <corundum/quest/system.hpp>
#include <corundum/render/render_state.hpp>
#include <corundum/render/render_system.hpp>
#include <corundum/settings/user_settings.hpp>
#include <corundum/shop/loader.hpp>
#include <corundum/shop/registry.hpp>
#include <corundum/ui/dialog_box.hpp>
#include <corundum/ui/hud_strip.hpp>
#include <corundum/ui/menu.hpp>
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
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

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
        load_content_registries();
        apply_starting_flags();
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

      void load_content_registries() {
        int dialogue_loaded{0};
        if (!engine_->cfg.paths.dialogue_dir.empty())
          dialogue_loaded = engine_->graphs.load_all(engine_->cfg.paths.dialogue_dir);
        corundum::detail::info_log("[engine] Loaded {} dialogue graphs from '{}'", dialogue_loaded,
                                   engine_->cfg.paths.dialogue_dir);

        int quest_loaded{0};
        if (!engine_->cfg.paths.quests_dir.empty())
          quest_loaded = engine_->quests.load_all(engine_->cfg.paths.quests_dir);
        corundum::detail::info_log("[engine] Loaded {} quests from '{}'", quest_loaded, engine_->cfg.paths.quests_dir);

        int item_loaded{0};
        if (!engine_->cfg.paths.items_dir.empty())
          item_loaded = engine_->items.load_all(engine_->cfg.paths.items_dir);
        corundum::detail::info_log("[engine] Loaded {} items from '{}'", item_loaded, engine_->cfg.paths.items_dir);

        int codex_loaded{0};
        if (!engine_->cfg.paths.codex_dir.empty())
          codex_loaded = engine_->codex.load_all(engine_->cfg.paths.codex_dir);
        corundum::detail::info_log("[engine] Loaded {} codex entries from '{}'", codex_loaded,
                                   engine_->cfg.paths.codex_dir);

        int location_loaded{0};
        if (!engine_->cfg.paths.locations_dir.empty())
          location_loaded = engine_->locations.load_all(engine_->cfg.paths.locations_dir);
        corundum::detail::info_log("[engine] Loaded {} locations from '{}'", location_loaded,
                                   engine_->cfg.paths.locations_dir);

        int shop_loaded{0};
        if (!engine_->cfg.paths.shops_dir.empty())
          shop_loaded = engine_->shops.load_all(engine_->cfg.paths.shops_dir);
        corundum::detail::info_log("[engine] Loaded {} shops from '{}'", shop_loaded, engine_->cfg.paths.shops_dir);

        validate_quest_references(engine_->graphs, engine_->quests, engine_->items);
        for (const auto &[shop_id, shop] : engine_->shops) {
          for (const shop::StockEntry &entry : shop.stock) {
            if (engine_->items.find(entry.item) == nullptr)
              warn_log("[engine] WARN: shop '{}' stocks unknown item '{}'", shop_id, entry.item);
          }
        }
      }

      /// Seed GameConfig::starting_flags into the FlagStore after the registries load, before
      /// the first frame. Runs inside initialize()'s success path, so a project's authored
      /// starting state (gold, reputation, "intro_seen") is in place before gameplay begins.
      void apply_starting_flags() {
        for (const auto &[key, value] : engine_->cfg.starting_flags)
          engine_->flags[key] = value;
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

    void handle_quest_start(Engine &engine, quest::Runner &quest_runner, const dialogue::EventAction &ev) {
      const bool already_started = quest::get_stage(ev.args[0], engine.flags) > 0;
      if (auto result = quest_runner.start(ev.args[0]); !result) {
        warn_log("[engine] WARN: {}", result.error());
        return;
      }
      if (already_started)
        return;
      if (const quest::Quest *quest = engine.quests.find(ev.args[0]); quest != nullptr)
        engine.notify(std::format("Quest started: {}", quest->name), ui::k_toast_default_colour);
    }

    void handle_quest_advance(Engine &engine, quest::Runner &quest_runner, const dialogue::EventAction &ev) {
      const int stage_before = quest::get_stage(ev.args[0], engine.flags);
      if (auto result = quest_runner.advance(ev.args[0], ev.args[1]); !result) {
        warn_log("[engine] WARN: {}", result.error());
        return;
      }
      // advance() reports ok even when the stage name was unknown (a logged no-op), so only
      // notify when the stage integer actually moved.
      if (quest::get_stage(ev.args[0], engine.flags) == stage_before)
        return;
      const quest::Quest *quest = engine.quests.find(ev.args[0]);
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
    void handle_unlock_codex(Engine &engine, const dialogue::EventAction &ev) {
      const std::string key{codex::flag_key(ev.args[0])};
      ui::codex_mark_dirty(engine.codex_screen);
      if (world::has_flag(engine.flags, key))
        return;
      world::set_flag(engine.flags, key);
      if (const codex::CodexEntry *entry = engine.codex.find(ev.args[0]); entry != nullptr)
        engine.notify(std::format("Codex updated: {}", entry->title), ui::k_toast_default_colour);
    }

    /// Discover a fast-travel location (`discover_location('id')`): set its discovery flag and
    /// notify only when it was not already known.
    void handle_discover_location(Engine &engine, const dialogue::EventAction &ev) {
      const std::string key{location::discovery_flag_key(ev.args[0])};
      if (world::has_flag(engine.flags, key))
        return;
      world::set_flag(engine.flags, key);
      if (const location::Location *location = engine.locations.find(ev.args[0]); location != nullptr)
        engine.notify(std::format("Location discovered: {}", location->name), ui::k_toast_default_colour);
    }

    /// Open a container's two-pane loot screen (`open_container('id')`). The screen is pushed onto
    /// the UI stack so closing it returns to whatever was beneath (dialogue included).
    void handle_open_container(Engine &engine, const dialogue::EventAction &ev) {
      engine.active_container_id = ev.args[0];
      engine.loot_screen = {};
      engine.scene.ui.push(world::GameMode::Loot);
    }

    /// Open a merchant's barter screen (`open_shop('id')`). Unknown ids warn and are ignored.
    void handle_open_shop(Engine &engine, const dialogue::EventAction &ev) {
      if (engine.shops.find(ev.args[0]) == nullptr) {
        warn_log("[engine] WARN: open_shop unknown shop '{}'", ev.args[0]);
        return;
      }
      engine.active_shop_id = ev.args[0];
      engine.barter_screen = {};
      engine.scene.ui.push(world::GameMode::Barter);
    }

    void dispatch_dialogue_event(Engine &engine, quest::Runner &quest_runner,
                                 const dialogue::EventAction &ev) noexcept {
      try {
        if (ev.name == "play_sound" && !ev.args.empty()) {
          handle_play_sound(engine, ev);
        } else if (ev.name == "quest_start" && !ev.args.empty()) {
          handle_quest_start(engine, quest_runner, ev);
        } else if (ev.name == "quest_advance" && ev.args.size() >= 2) {
          handle_quest_advance(engine, quest_runner, ev);
        } else if (ev.name == "give_item" && !ev.args.empty()) {
          engine.flags[item_flag_key(ev.args[0])] += event_int_arg(ev, 1, /*fallback=*/1);
        } else if (ev.name == "take_item" && !ev.args.empty()) {
          handle_take_item(engine, ev);
        } else if (ev.name == "unlock_codex" && !ev.args.empty()) {
          handle_unlock_codex(engine, ev);
        } else if (ev.name == "discover_location" && !ev.args.empty()) {
          handle_discover_location(engine, ev);
        } else if (ev.name == "open_container" && !ev.args.empty()) {
          handle_open_container(engine, ev);
        } else if (ev.name == "open_shop" && !ev.args.empty()) {
          handle_open_shop(engine, ev);
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
      render::configure_dialog_style(engine.render, engine.cfg);
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

    /// Step the Codex screen: Back/C closes it, Up/Down move the highlighted entry, and the
    /// scroll wheel scrolls the detail body. Rows are refreshed from the registry + flags on the
    /// first step after an open or unlock (dirty-flagged).
    void update_codex(Engine &engine, const input::InputIntent &intent) {
      ui::CodexState &state = engine.codex_screen;
      ui::refresh_codex(state, engine.codex, engine.flags);

      if (intent.back || engine.input_state.is_pressed(input::Action::Codex)) {
        engine.scene.ui.pop();
        return;
      }

      const int rows = static_cast<int>(state.entries.size());
      if (intent.navigate_y != 0) {
        if (rows > 0)
          state.cursor = wrap_cursor(state.cursor, intent.navigate_y, rows);
        state.scroll = 0.f;
      }
      if (intent.scroll_y != 0.f)
        state.scroll = std::max(0.f, state.scroll - intent.scroll_y);
    }

    /// Step the Map screen: Back/M closes it, Up/Down move the highlighted destination, and
    /// Activate fast-travels. A destination already in the active zone is a no-op.
    void update_map(Engine &engine, const input::InputIntent &intent) {
      if (intent.back || engine.input_state.is_pressed(input::Action::Map)) {
        engine.scene.ui.pop();
        return;
      }

      const std::vector<ui::MapEntry> entries =
          ui::build_map_entries(engine.locations, engine.flags, engine.scene.zone_id);
      const int rows = static_cast<int>(entries.size());
      if (intent.navigate_y != 0 && rows > 0)
        engine.map_screen.cursor = wrap_cursor(engine.map_screen.cursor, intent.navigate_y, rows);
      if (!intent.activate || rows == 0)
        return;

      const ui::MapEntry &selected = entries[static_cast<std::size_t>(
          std::clamp(engine.map_screen.cursor, 0, rows - 1))];
      if (selected.current) {
        engine.notify("You are already here");
        return;
      }

      const location::Location *location = engine.locations.find(selected.id);
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

    /// Step the loot screen: Back closes, Left/Right switch the active pane, Up/Down move the
    /// row, and Activate moves one unit of the highlighted item to the other holder.
    void update_loot(Engine &engine, const input::InputIntent &intent) {
      if (intent.back) {
        engine.active_container_id.clear();
        engine.scene.ui.pop();
        return;
      }

      const std::string container_prefix = item::container_flag_prefix(engine.active_container_id);
      if (intent.navigate_x != 0) {
        engine.loot_screen.pane =
            engine.loot_screen.pane == ui::LootPane::Container ? ui::LootPane::Player : ui::LootPane::Container;
        engine.loot_screen.cursor = 0;
      }

      const bool container_active = engine.loot_screen.pane == ui::LootPane::Container;
      const std::vector<ui::InventoryLine> container_lines =
          ui::build_item_lines(engine.flags, engine.items, container_prefix);
      const std::vector<ui::InventoryLine> player_lines =
          ui::build_item_lines(engine.flags, engine.items, item::k_flag_prefix);
      const std::vector<ui::InventoryLine> &lines = container_active ? container_lines : player_lines;
      const int rows = static_cast<int>(lines.size());

      if (intent.navigate_y != 0 && rows > 0)
        engine.loot_screen.cursor = wrap_cursor(engine.loot_screen.cursor, intent.navigate_y, rows);
      if (!intent.activate || rows == 0)
        return;

      const ui::InventoryLine &selected = lines[static_cast<std::size_t>(
          std::clamp(engine.loot_screen.cursor, 0, rows - 1))];
      const std::string container_key = item::container_item_flag_key(engine.active_container_id, selected.id);
      const std::string player_key = item_flag_key(selected.id);
      if (container_active) {
        adjust_flag(engine.flags, container_key, -1);
        adjust_flag(engine.flags, player_key, 1);
      } else {
        adjust_flag(engine.flags, player_key, -1);
        adjust_flag(engine.flags, container_key, 1);
      }
    }

    /// Step the barter screen: Back closes, Left/Right or Tab switch Buy/Sell, Up/Down move the
    /// row, and Activate performs the trade.
    void update_barter(Engine &engine, const input::InputIntent &intent) {
      if (intent.back) {
        engine.active_shop_id.clear();
        engine.scene.ui.pop();
        return;
      }

      const shop::Shop *shop = engine.shops.find(engine.active_shop_id);
      if (shop == nullptr) {
        engine.scene.ui.pop();
        return;
      }

      if (intent.next_tab || intent.prev_tab || intent.navigate_x != 0) {
        engine.barter_screen.tab =
            engine.barter_screen.tab == ui::BarterTab::Buy ? ui::BarterTab::Sell : ui::BarterTab::Buy;
        engine.barter_screen.cursor = 0;
      }

      const int reputation =
          shop->faction.empty() ? 0 : world::visit_count(engine.flags, std::string{"rep."} + shop->faction);
      const std::vector<ui::BarterLine> lines = engine.barter_screen.tab == ui::BarterTab::Buy
                                                    ? ui::build_barter_stock(*shop, engine.items, reputation)
                                                    : ui::build_barter_sell_lines(*shop, engine.items, engine.flags);
      const int rows = static_cast<int>(lines.size());

      if (intent.navigate_y != 0 && rows > 0)
        engine.barter_screen.cursor = wrap_cursor(engine.barter_screen.cursor, intent.navigate_y, rows);
      if (!intent.activate || rows == 0)
        return;

      const ui::BarterLine &selected = lines[static_cast<std::size_t>(
          std::clamp(engine.barter_screen.cursor, 0, rows - 1))];
      int gold = world::visit_count(engine.flags, std::string{ui::k_gold_flag});
      if (engine.barter_screen.tab == ui::BarterTab::Buy) {
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
      engine.flags[std::string{ui::k_gold_flag}] = gold;
    }

    /// Upper bound on catch-up work per frame. At 60 Hz this is ~133 ms of catch-up; past it the
    /// simulation sheds the remaining time rather than compounding a backlog.
    constexpr int k_max_steps_per_frame{8};

    /// Drain the timer accumulator: run gameplay, dialogue events, the
    /// on_fixed_update hook, and deletion flushing once per fixed step.
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
        // The engine-owned Menu/Settings screens own the step while open, which is what pauses the
        // simulation — no world update, dialogue dispatch, quest tick, or on_fixed_update while a
        // menu is up. Call it unconditionally: it has side effects (opening the menu) even when no
        // screen ends up active. Opening the pause menu happens here because Esc doubles as
        // Cancel — opening consumes the step so the same press cannot also close the fresh menu.
        const bool engine_screen_active = engine.update_engine_screens(intent);

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
            world::update(engine.scene, engine.cfg, engine.graphs, engine.input_state, map_view,
                          engine.timer.target_dt, static_cast<float>(engine.window_width()),
                          static_cast<float>(engine.window_height()), engine.flags, &engine.quests,
                          engine.input_mapper.last_device());
          }

          // Dialogue, quests, and the hook run every step — including World mode with nothing
          // streamed in — so queued work is never stranded while elapsed_time advances.
          engine.process_dialogue_events();
          engine.toasts.update(engine.timer.target_dt);
          if (engine.scene.dialogue)
            ui::dialog_box_advance(engine.render.dialog_box, *engine.scene.dialogue, engine.timer.target_dt);
          quest::tick_quests(engine.quests, engine.flags, engine.scene.zone_id);
          invoke_fixed_update_hook(engine, engine.timer.target_dt);

          entities::flush_deletions(engine.scene.world);
        }

        input::clear_pressed(engine.input_state);
      }

      return result;
    }

    /// begin_frame → world/UI render → optional debug HUD → end_frame.
    void render_frame(Engine &engine, const float alpha, const bool budget_exhausted) noexcept {
      if (!engine.renderer->begin_frame(engine.clear_colour))
        return;
      render::render(*engine.renderer, engine.render, engine.cfg, engine.scene, engine.flags, &engine.items,
                     &engine.quests, &engine.toasts, alpha, engine.window_width(), engine.window_height(),
                     engine.input_mapper.last_device());

      // The engine-owned Menu/Settings screens draw over the world overlays. They live here rather
      // than inside render::render so they can read the live bindings and audio volume without
      // threading those through the render signature.
      const corundum::core::math::Vec2 viewport{
          .x = static_cast<float>(engine.window_width()),
          .y = static_cast<float>(engine.window_height()),
      };
      switch (engine.scene.mode()) {
        case world::GameMode::Codex:
          ui::codex_panel_render(*engine.renderer, engine.render.dialog_box.style, engine.render.dialog_box.border,
                                 engine.codex_screen.entries, engine.codex_screen.cursor, engine.codex_screen.scroll,
                                 viewport, engine.input_mapper.last_device());
          break;
        case world::GameMode::Map:
          ui::map_panel_render(*engine.renderer, engine.render.dialog_box.style, engine.render.dialog_box.border,
                               ui::build_map_entries(engine.locations, engine.flags, engine.scene.zone_id),
                               engine.map_screen.cursor, viewport, engine.input_mapper.last_device());
          break;
        case world::GameMode::Loot: {
          const std::string container_name =
              engine.active_container_id.empty() ? std::string{"Container"} : engine.active_container_id;
          ui::loot_panel_render(
              *engine.renderer, engine.render.dialog_box.style, engine.render.dialog_box.border, container_name,
              ui::build_item_lines(engine.flags, engine.items,
                                   item::container_flag_prefix(engine.active_container_id)),
              ui::build_item_lines(engine.flags, engine.items, item::k_flag_prefix), engine.loot_screen, viewport,
              engine.input_mapper.last_device());
          break;
        }
        case world::GameMode::Barter: {
          const shop::Shop *shop = engine.shops.find(engine.active_shop_id);
          if (shop == nullptr)
            break;
          const int reputation =
              shop->faction.empty() ? 0 : world::visit_count(engine.flags, std::string{"rep."} + shop->faction);
          const std::vector<ui::BarterLine> lines = engine.barter_screen.tab == ui::BarterTab::Buy
                                                        ? ui::build_barter_stock(*shop, engine.items, reputation)
                                                        : ui::build_barter_sell_lines(*shop, engine.items, engine.flags);
          ui::barter_panel_render(*engine.renderer, engine.render.dialog_box.style,
                                  engine.render.dialog_box.border, shop->name,
                                  world::visit_count(engine.flags, std::string{ui::k_gold_flag}), lines,
                                  engine.barter_screen, viewport, engine.input_mapper.last_device());
          break;
        }
        case world::GameMode::Menu:
          ui::menu_panel_render(*engine.renderer, engine.render.dialog_box.style, engine.render.dialog_box.border,
                                engine.menu, viewport, engine.input_mapper.last_device());
          break;
        case world::GameMode::Settings:
          ui::settings_panel_render(*engine.renderer, engine.render.dialog_box.style, engine.render.dialog_box.border,
                                    engine.settings_screen, settings_values(engine), engine.input_mapper.bindings(),
                                    viewport, engine.input_mapper.last_device());
          break;
        default:
          break;
      }

      const debug::OverlayInput hud_input{
          .render_state = &engine.render,
          .cfg = &engine.cfg,
          .scene = &engine.scene,
          .timer = &engine.timer,
          .viewport =
              {
                  .x = static_cast<float>(engine.window_width()),
                  .y = static_cast<float>(engine.window_height()),
              },
          .step_budget_exhausted = budget_exhausted,
      };
      engine.hud.render(*engine.renderer, hud_input);

      engine.renderer->end_frame();
    }

  } // namespace

  bool Engine::update_engine_screens(const input::InputIntent &intent) {
    using world::GameMode;

    // Opening a screen consumes the step: the same press that opened it must not also be
    // routed to the fresh screen (C would open then immediately close the codex).
    bool opened_screen = false;
    if (input_state.is_pressed(input::Action::Menu) && scene.mode() == GameMode::Exploring) {
      scene.ui.push(GameMode::Menu);
      menu.cursor = 0;
      opened_screen = true;
    }

    if (input_state.is_pressed(input::Action::Codex) && scene.mode() == GameMode::Exploring) {
      scene.ui.push(GameMode::Codex);
      codex_screen.cursor = 0;
      codex_screen.scroll = 0.f;
      ui::codex_mark_dirty(codex_screen);
      ui::refresh_codex(codex_screen, codex, flags);
      opened_screen = true;
    }

    if (input_state.is_pressed(input::Action::Map) && scene.mode() == GameMode::Exploring) {
      scene.ui.push(GameMode::Map);
      map_screen.cursor = 0;
      opened_screen = true;
    }

    if (opened_screen)
      return true;

    switch (scene.mode()) {
      case GameMode::Codex:
        update_codex(*this, intent);
        return true;
      case GameMode::Map:
        update_map(*this, intent);
        return true;
      case GameMode::Loot:
        update_loot(*this, intent);
        return true;
      case GameMode::Barter:
        update_barter(*this, intent);
        return true;
      case GameMode::Menu:
        update_pause_menu(*this, intent);
        return true;
      case GameMode::Settings:
        update_settings(*this, intent);
        return true;
      default:
        return false;
    }
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
