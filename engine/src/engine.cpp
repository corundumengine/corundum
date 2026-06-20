#include <corundum/debug/debug_overlay.hpp>
#include <corundum/engine.hpp>
#include <corundum/gameplay/ecs/world.hpp>
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

  std::expected<void, std::string> initialize(Engine &engine, std::string_view config_path) {
    {
      auto cfg_result = core::load_game_config(config_path);
      if (!cfg_result) {
        engine.window->close();
        return std::unexpected(std::format("[engine] FATAL: {}", cfg_result.error()));
      }
      engine.cfg = std::move(*cfg_result);
    }
    engine.timer.set_target_fps(static_cast<float>(engine.cfg.framerate));
    engine.window->set_vsync(engine.cfg.vsync);
    engine.window->resize(static_cast<unsigned>(engine.cfg.win_w), static_cast<unsigned>(engine.cfg.win_h));

    {
      auto char_result = engine.characters.load_all(engine.cfg.paths.sprites_dir);
      if (!char_result) {
        engine.window->close();
        return std::unexpected(std::format("[engine] FATAL: {}", char_result.error()));
      }
    }
    render::sys::load_sprite_index(*engine.renderer, engine.render, engine.characters);

    const auto font_path = std::format("{}/{}", engine.cfg.paths.font_dir, engine.cfg.paths.game_font);
    auto font_result = render::sys::load_font(*engine.renderer, engine.render, font_path);
    if (!font_result.has_value()) {
      engine.window->close();
      return std::unexpected(std::format("[engine] FATAL: {}", font_result.error()));
    }

    {
      auto ui_result = render::sys::load_ui_assets(*engine.renderer, engine.render);
      if (!ui_result) {
        engine.window->close();
        return std::unexpected(std::format("[engine] FATAL: {}", ui_result.error()));
      }

      if (!engine.cfg.paths.world_manifest_path.empty()) {
        auto world_result = render::sys::load_world(*engine.renderer, engine.render, engine.cfg);
        if (!world_result) {
          engine.window->close();
          return std::unexpected(std::format("[engine] FATAL: {}", world_result.error()));
        }
        const auto &info = *world_result;
        const auto spawn_pos = gameplay::ecs::Position{info.spawn_world_pos.x, info.spawn_world_pos.y};
        auto scene_result = gameplay::world::spawn_world(engine.cfg, engine.characters,
                                                         engine.render.active_chunks[0].tilemap, spawn_pos);
        if (!scene_result) {
          engine.window->close();
          return std::unexpected(std::format("[engine] FATAL: {}", scene_result.error()));
        }
        engine.scene = std::move(*scene_result);
        const auto [ww, wh] = gameplay::world::tilemap::world_bounds_iso(engine.render.manifest, info.half_tw);
        engine.scene.camera.x =
            std::clamp(info.spawn_world_pos.x - engine.cfg.win_w * 0.5f, 0.f, ww - engine.cfg.win_w);
        engine.scene.camera.y =
            std::clamp(info.spawn_world_pos.y - engine.cfg.win_h * 0.5f, 0.f, wh - engine.cfg.win_h);
      } else {
        auto map_result =
            render::sys::load_map(*engine.renderer, engine.render, engine.cfg.paths.tilemap_path, engine.cfg);
        if (!map_result) {
          engine.window->close();
          return std::unexpected(std::format("[engine] FATAL: {}", map_result.error()));
        }
        {
          auto scene_result =
              gameplay::world::spawn_world(engine.cfg, engine.characters, engine.render.map_data.tilemap);
          if (!scene_result) {
            engine.window->close();
            return std::unexpected(std::format("[engine] FATAL: {}", scene_result.error()));
          }
          engine.scene = std::move(*scene_result);
        }
      }
    }

    render::sys::configure_dialog_style(engine.render, engine.cfg);

    const auto loaded = engine.graphs.load_all(engine.cfg.paths.dialogue_dir);
    std::println("[engine] Loaded {} dialogue graphs", loaded);
    return {};
  }

  void update(Engine &engine) noexcept {
    while (engine.window->is_open() && !engine.quit) {
      {
        const auto &transforms = engine.scene.ecs_world.transforms;
        const auto n = transforms.count;
        if (engine.render.prev_x.size() < n) {
          engine.render.prev_x.resize(n);
          engine.render.prev_y.resize(n);
        }
        for (std::uint32_t i = 0; i < n; ++i) {
          engine.render.prev_x[i] = transforms.x[i];
          engine.render.prev_y[i] = transforms.y[i];
        }
      }

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
        gameplay::ecs::flush_deletions(engine.scene.ecs_world);
        input::clear_pressed(engine.input_state);
      }

      if (engine.render.mode != render::data::RenderMode::World)
        gameplay::world::handle_map_transition(engine);

      engine.render.interpolation_alpha = (steps_run == 1) ? engine.timer.alpha() : 0.f;

      engine.renderer->begin_frame(engine.clear_colour);
      render::sys::render(*engine.renderer, engine.render, engine.cfg, engine.scene, engine.render.interpolation_alpha);

      if (engine.show_debug_hud) {
        debug::OverlayInput hud_input{
            .render_state = engine.render,
            .cfg = engine.cfg,
            .scene = engine.scene,
            .timer = engine.timer,
            .smoothed_fps = engine.smoothed_fps,
        };
        debug::draw_overlays(*engine.renderer, hud_input);
      }

      engine.renderer->end_frame();
    }
  }

  void cleanup(Engine &engine) noexcept {
    engine.window->close();
  }

  void request_quit(Engine &engine) noexcept {
    engine.quit = true;
  }

} // namespace corundum
