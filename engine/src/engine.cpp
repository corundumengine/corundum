#include <corundum/debug/debug_overlay.hpp>
#include <corundum/engine.hpp>
#include <corundum/gameplay/entity/world.hpp>
#include <corundum/gameplay/quest/system.hpp>
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
      const auto [ww, wh] =
          gameplay::world::tilemap::world_bounds_iso(engine.render.manifest, info.half_tw, info.half_th);
      // Convert tile-grid spawn position to isometric for camera tracking.
      const float iso_spawn_x = (spawn_pos.col - spawn_pos.row) * info.half_tw + info.x_origin;
      const float iso_spawn_y = (spawn_pos.col + spawn_pos.row) * info.half_th;
      engine.scene.camera.x = std::clamp(iso_spawn_x - cfg.win_w * 0.5f, 0.f, ww - cfg.win_w);
      engine.scene.camera.y = std::clamp(iso_spawn_y - cfg.win_h * 0.5f, 0.f, wh - cfg.win_h);
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

      // Initialize camera centered on the player.
      const auto &tm = engine.render.map_data.tilemap;
      const auto iso = core::math::compute_iso_params(tm.diamond_w(), tm.diamond_h(), tm.height, cfg.tile_scale);
      const auto p_slot = engine.scene.world.transforms.dense_idx(engine.scene.player);
      const float pc = engine.scene.world.transforms.col[p_slot];
      const float pr = engine.scene.world.transforms.row[p_slot];
      const auto iso_pos = core::math::tile_to_world(pc, pr, 0, iso, 0.f);
      const float iso_x = iso_pos.x;
      const float iso_y = iso_pos.y;
      const float extent = static_cast<float>(tm.width + tm.height - 1) * iso.half_tw * 2.f;
      engine.scene.camera.x = std::clamp(iso_x - cfg.win_w * 0.5f, 0.f, extent - cfg.win_w);
      engine.scene.camera.y = std::clamp(iso_y - cfg.win_h * 0.5f, 0.f, extent - cfg.win_h);
      return {};
    }

    void validate_quest_references(const corundum::gameplay::dialogue::Registry &graphs,
                                   const corundum::gameplay::quest::Registry &quests) {
      for (const auto &[id, graph] : graphs) {
        for (const auto &node : graph.nodes) {
          for (const auto &action : node.actions) {
            const auto parsed = corundum::gameplay::dialogue::parse_action(action);
            if (!parsed)
              continue;

            const auto *ev = std::get_if<corundum::gameplay::dialogue::EventAction>(&*parsed);
            if (!ev)
              continue;

            if (ev->name == "quest_start" && !ev->args.empty()) {
              if (!quests.find(ev->args[0]))
                std::println(stderr, "[engine] WARN: dialogue '{}' quest_start(\"{}\") references unknown quest", id,
                             ev->args[0]);
            }

            if (ev->name == "quest_advance" && ev->args.size() >= 2) {
              if (!quests.find(ev->args[0])) {
                std::println(stderr, "[engine] WARN: dialogue '{}' quest_advance(\"{}\") references unknown quest", id,
                             ev->args[0]);
              } else if (const auto *q = quests.find(ev->args[0])) {
                if (!q->find_stage(ev->args[1]))
                  std::println(stderr,
                               "[engine] WARN: dialogue '{}' quest_advance(\"{}\", \"{}\") references unknown stage",
                               id, ev->args[0], ev->args[1]);
              }
            }
          }
        }
      }
    }

  } // namespace

  void process_dialogue_events(Engine &engine) noexcept {
    for (const auto &ev : engine.scene.pending_dialogue_events) {
      if (ev.name == "play_sound" && !ev.args.empty()) {
        const auto result = audio::sys::play_sound(engine.audio, ev.args[0]);
        if (!result)
          std::println("[engine] {}", result.error());
      } else if (ev.name == "quest_start" && !ev.args.empty()) {
        if (const auto *q = engine.quests.find(ev.args[0]))
          gameplay::quest::start(*q, engine.flags);
        else
          std::println(stderr, "[engine] WARN: quest_start(\"{}\") references unknown quest", ev.args[0]);
      } else if (ev.name == "quest_advance" && ev.args.size() >= 2) {
        if (const auto *q = engine.quests.find(ev.args[0]))
          gameplay::quest::advance(*q, ev.args[1], engine.flags);
        else
          std::println(stderr, "[engine] WARN: quest_advance(\"{}\", \"{}\") references unknown quest", ev.args[0],
                       ev.args[1]);
      } else {
        bool handled = false;
        if (engine.on_event)
          handled = engine.on_event(engine, ev);
        if (!handled)
          std::println(stderr, "[engine] WARN: unknown dialogue event '{}'", ev.name);
      }
    }
    engine.scene.pending_dialogue_events.clear();
  }

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

    int dialogue_loaded = 0;
    if (!engine.cfg.paths.dialogue_dir.empty())
      dialogue_loaded = engine.graphs.load_all(engine.cfg.paths.dialogue_dir);
    std::println("[engine] Loaded {} dialogue graphs", dialogue_loaded);

    int quest_loaded = 0;
    if (!engine.cfg.paths.quests_dir.empty())
      quest_loaded = engine.quests.load_all(engine.cfg.paths.quests_dir);
    std::println("[engine] Loaded {} quests", quest_loaded);
    validate_quest_references(engine.graphs, engine.quests);
    return {};
  }

  void update(Engine &engine) noexcept {
    while (engine.window->is_open() && !engine.quit) {
      std::tie(engine.win_w, engine.win_h) = engine.window->size();

      const auto &transforms = engine.scene.world.transforms;
      const auto n = transforms.count;
      for (std::uint32_t i = 0; i < n; ++i) {
        engine.render.prev_col[i] = transforms.col[i];
        engine.render.prev_row[i] = transforms.row[i];
      }
      engine.render.prev_count = n;
      engine.render.prev_cam_x = engine.scene.camera.x;
      engine.render.prev_cam_y = engine.scene.camera.y;
      engine.render.prev_zoom = engine.scene.camera.zoom;

      engine.timer.tick();

      input::sys::poll(engine.input_state, *engine.window);
      if (engine.input_state.is_held(input::Action::Quit)) {
        request_quit(engine);
        engine.window->close();
      }

      int steps_run = 0;
      bool deleted_this_frame = false;
      while (engine.timer.step()) {
        ++steps_run;
        engine.scene.elapsed_time += engine.timer.target_dt;
        if (engine.render.mode == render::data::RenderMode::World && engine.render.active_chunks.empty())
          continue;

        const auto mv = gameplay::world::build_map_view(engine.render, engine.cfg);
        gameplay::world::update(engine.scene, engine.cfg, engine.graphs, engine.input_state, mv, engine.timer.target_dt,
                                static_cast<float>(engine.win_w), static_cast<float>(engine.win_h), engine.flags,
                                &engine.quests);

        process_dialogue_events(engine);

        if (engine.on_fixed_update)
          engine.on_fixed_update(engine, engine.timer.target_dt);

        // prev_col/prev_row were snapshotted by dense slot before this loop started. flush_deletions
        // reassigns slots via swap-and-pop, so a slot interpolated below could now belong to a
        // different entity than the one the snapshot captured. Detect it here and force alpha to 0
        // below rather than interpolate against a stale/mismatched slot.
        deleted_this_frame = deleted_this_frame || engine.scene.world.pending_deletion_count > 0;
        gameplay::entity::flush_deletions(engine.scene.world);

        input::clear_pressed(engine.input_state);
      }

      if (engine.render.mode != render::data::RenderMode::World)
        gameplay::world::handle_map_transition(engine);

      engine.render.interpolation_alpha = (steps_run == 1 && !deleted_this_frame) ? engine.timer.alpha() : 0.f;

      engine.renderer->begin_frame(engine.clear_colour);
      render::sys::render(*engine.renderer, engine.render, engine.cfg, engine.scene, engine.flags,
                          engine.render.interpolation_alpha, engine.win_w, engine.win_h);

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

      // Load one pending chunk per frame — queues I/O between frames so
      // chunk-boundary loads don't hitch the render pass.
      if (engine.render.mode == render::data::RenderMode::World)
        render::sys::load_one_pending_chunk(*engine.renderer, engine.render, engine.cfg);
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
