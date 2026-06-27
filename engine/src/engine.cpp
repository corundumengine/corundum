#include <corundum/debug/debug_overlay.hpp>
#include <corundum/engine.hpp>
#include <corundum/gameplay/entity/world.hpp>
#include <corundum/gameplay/world/map_view.hpp>
#include <corundum/gameplay/world/spawn.hpp>
#include <corundum/gameplay/world/tilemap/world_manifest.hpp>
#include <corundum/gameplay/world/transition.hpp>
#include <corundum/gameplay/world/update.hpp>
#include <corundum/input/sys/input_sys.hpp>
#include <corundum/platform/platform_factory.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/platform/window.hpp>
#include <corundum/render/sys/render_sys.hpp>

#include <format>
#include <print>

namespace corundum {

  namespace {

    std::expected<void, std::string> init_world(Engine &engine, const core::GameConfig &cfg) {
      auto world_result = render::sys::load_world(*engine.renderer, engine.render, cfg);
      if (!world_result)
        return std::unexpected(world_result.error());
      const auto &info = *world_result;
      const auto spawn_pos = gameplay::component::Position{info.spawn_world_pos.x, info.spawn_world_pos.y};
      auto scene_result =
          gameplay::world::spawn_world(cfg, engine.characters, engine.render.active_chunks[0].tilemap, spawn_pos);
      if (!scene_result)
        return std::unexpected(scene_result.error());
      engine.scene = std::move(*scene_result);
      const auto [ww, wh] = gameplay::world::tilemap::world_bounds_iso(engine.render.manifest, info.half_tw);
      engine.scene.camera.x = std::clamp(info.spawn_world_pos.x - cfg.win_w * 0.5f, 0.f, ww - cfg.win_w);
      engine.scene.camera.y = std::clamp(info.spawn_world_pos.y - cfg.win_h * 0.5f, 0.f, wh - cfg.win_h);
      return {};
    }

    std::expected<void, std::string> init_single_map(Engine &engine, const core::GameConfig &cfg) {
      auto map_result = render::sys::load_map(*engine.renderer, engine.render, cfg.paths.tilemap_path, cfg);
      if (!map_result)
        return std::unexpected(map_result.error());
      auto scene_result = gameplay::world::spawn_world(cfg, engine.characters, engine.render.map_data.tilemap);
      if (!scene_result)
        return std::unexpected(scene_result.error());
      engine.scene = std::move(*scene_result);
      return {};
    }

    void process_dialogue_events(audio::sys::AudioState &audio,
                                 std::vector<corundum::gameplay::dialogue::EventAction> &events) {
      for (const auto &ev : events) {
        if (ev.name == "play_sound" && !ev.args.empty()) {
          std::expected<void, std::string> result = audio::sys::play_sound(audio, ev.args[0]);
          if (!result)
            std::println("[engine] {}", result.error());
        }
      }
      events.clear();
    }

  } // namespace

  std::expected<void, std::string> initialize(Engine &engine, core::GameConfig &&cfg) {
    engine.cfg = std::move(cfg);
    engine.timer.set_target_fps(static_cast<float>(engine.cfg.framerate));
    engine.window->set_vsync(engine.cfg.vsync);
    engine.window->resize(static_cast<unsigned>(engine.cfg.win_w), static_cast<unsigned>(engine.cfg.win_h));

    auto char_result = engine.characters.load_all(engine.cfg.paths.sprites_dir);
    if (!char_result) {
      engine.window->close();
      return std::unexpected(std::format("[engine] FATAL: {}", char_result.error()));
    }
    render::sys::load_sprite_index(*engine.renderer, engine.render, engine.characters);

    const auto font_path = std::format("{}/{}", engine.cfg.paths.font_dir, engine.cfg.paths.game_font);
    auto font_result = render::sys::load_font(*engine.renderer, engine.render, font_path);
    if (!font_result.has_value()) {
      engine.window->close();
      return std::unexpected(std::format("[engine] FATAL: {}", font_result.error()));
    }

    auto ui_result = render::sys::load_ui_assets(*engine.renderer, engine.render);
    if (!ui_result) {
      engine.window->close();
      return std::unexpected(std::format("[engine] FATAL: {}", ui_result.error()));
    }

    if (!engine.cfg.paths.world_manifest_path.empty()) {
      if (auto result = init_world(engine, engine.cfg); !result) {
        engine.window->close();
        return std::unexpected(std::format("[engine] FATAL: {}", result.error()));
      }
    } else {
      if (auto result = init_single_map(engine, engine.cfg); !result) {
        engine.window->close();
        return std::unexpected(std::format("[engine] FATAL: {}", result.error()));
      }
    }

    engine.audio.sounds_dir = engine.cfg.paths.sounds_dir;
    std::expected<void, std::string> audio_result = audio::sys::initialize(engine.audio);
    if (!audio_result)
      std::println("[engine] WARN: Audio init failed — {}", audio_result.error());
    else
      audio::sys::load_catalog(engine.audio, engine.cfg.paths.sounds_catalog);

    render::sys::configure_dialog_style(engine.render, engine.cfg);

    const auto loaded = engine.graphs.load_all(engine.cfg.paths.dialogue_dir);
    std::println("[engine] Loaded {} dialogue graphs", loaded);
    return {};
  }

  void update(Engine &engine) noexcept {
    while (engine.window->is_open() && !engine.quit) {
      const auto &transforms = engine.scene.world.transforms;
      const auto n = transforms.count;
      if (engine.render.prev_x.size() < n) {
        engine.render.prev_x.resize(n);
        engine.render.prev_y.resize(n);
      }
      for (std::uint32_t i = 0; i < n; ++i) {
        engine.render.prev_x[i] = transforms.x[i];
        engine.render.prev_y[i] = transforms.y[i];
      }
      engine.render.prev_cam_x = engine.scene.camera.x;
      engine.render.prev_cam_y = engine.scene.camera.y;

      engine.timer.tick();

      input::sys::poll(engine.input_state, *engine.window);
      if (engine.input_state.is_held(input::Action::Quit)) {
        request_quit(engine);
        engine.window->close();
      }

      int steps_run = 0;
      while (engine.timer.step()) {
        ++steps_run;
        engine.scene.elapsed_time += engine.timer.target_dt;
        if (engine.render.mode == render::data::RenderMode::World && engine.render.active_chunks.empty())
          continue;

        const auto mv = gameplay::world::build_map_view(engine.render, engine.cfg);
        gameplay::world::update(engine.scene, engine.cfg, engine.graphs, engine.input_state, mv,
                                engine.timer.target_dt);

        process_dialogue_events(engine.audio, engine.scene.pending_dialogue_events);

        gameplay::entity::flush_deletions(engine.scene.world);
        input::clear_pressed(engine.input_state);
      }

      if (engine.render.mode != render::data::RenderMode::World)
        gameplay::world::handle_map_transition(engine);

      engine.render.interpolation_alpha = (steps_run == 1) ? engine.timer.alpha() : 0.f;

      engine.renderer->begin_frame(engine.clear_colour);
      render::sys::render(*engine.renderer, engine.render, engine.cfg, engine.scene, engine.render.interpolation_alpha);

      if (engine.show_debug_hud) {
        const debug::OverlayInput hud_input{
            .render_state = engine.render,
            .cfg = engine.cfg,
            .scene = engine.scene,
            .timer = engine.timer,
        };
        debug::draw_overlays(*engine.renderer, hud_input, engine.smoothed_fps);
      }

      engine.renderer->end_frame();
    }
  }

  void cleanup(Engine &engine) noexcept {
    audio::sys::shutdown(engine.audio);
    engine.window->close();
  }

  void request_quit(Engine &engine) noexcept {
    engine.quit = true;
  }

} // namespace corundum
