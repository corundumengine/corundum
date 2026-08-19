#include <corundum/debug/debug_overlay.hpp>
#include <corundum/engine.hpp>
#include <corundum/gameplay/dialogue/validate_refs.hpp>
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
      auto scene_result = gameplay::world::spawn_world(cfg, engine.characters, *active_tilemap(engine));
      if (!scene_result)
        return std::unexpected(scene_result.error());
      engine.scene = std::move(*scene_result);

      // Initialize camera centered on the player.
      const auto &tm = *active_tilemap(engine);
      const auto iso = core::math::compute_isometric_params(tm.diamond_w(), tm.diamond_h(), tm.height, cfg.tile_scale, cfg.elevation_step_px);
      const auto p_slot = engine.scene.world.transforms.dense_idx(engine.scene.player);
      const float pc = engine.scene.world.transforms.col[p_slot];
      const float pr = engine.scene.world.transforms.row[p_slot];
      const auto iso_pos = core::math::tile_to_world(pc, pr, 0, iso);
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
        for (const auto &err : corundum::gameplay::dialogue::validate_quest_refs(graph, quests))
          std::println(stderr, "[engine] WARN: dialogue '{}' {}", id, err);
        for (const auto &err : corundum::gameplay::dialogue::validate_condition_quest_refs(graph, quests))
          std::println(stderr, "[engine] WARN: dialogue '{}' {}", id, err);
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
    if (!engine.window || !engine.gpu || !engine.renderer)
      return std::unexpected("initialize: engine.window, engine.gpu and engine.renderer must be non-null "
                             "(use make_engine(), or adopt a platform before calling)");

    const auto fail = [&engine](const std::string &msg) {
      cleanup(engine);
      return std::unexpected(msg);
    };

    // Timing + window.
    engine.cfg = std::move(cfg);
    engine.timer.set_target_fps(static_cast<float>(engine.cfg.framerate));
    engine.window->set_vsync(engine.cfg.vsync);
    engine.window->resize(static_cast<unsigned>(engine.cfg.win_w), static_cast<unsigned>(engine.cfg.win_h));

    // Render assets.
    auto char_result = engine.characters.load_all(engine.cfg.paths.sprites_dir);
    if (!char_result)
      return fail(char_result.error());
    render::sys::load_sprite_index(*engine.renderer, engine.render, engine.characters);

    const auto font_path = std::format("{}/{}", engine.cfg.paths.font_dir, engine.cfg.paths.game_font);
    auto font_result = render::sys::load_font(*engine.renderer, engine.render, font_path);
    if (!font_result)
      return fail(font_result.error());

    auto ui_result = render::sys::load_ui_assets(*engine.renderer, engine.render);
    if (!ui_result)
      return fail(ui_result.error());

    // Scene: world mode when a manifest is configured, single-map mode otherwise.
    const bool world_mode = !engine.cfg.paths.world_manifest_path.empty();
    if (auto result = world_mode ? init_world(engine, engine.cfg) : init_single_map(engine, engine.cfg); !result)
      return fail(result.error());

    // Audio.
    engine.audio.sounds_dir = engine.cfg.paths.sounds_dir;
    std::expected<void, std::string> audio_result = audio::sys::initialize(engine.audio);
    if (!audio_result)
      std::println("[engine] WARN: Audio init failed — {}", audio_result.error());
    else
      audio::sys::load_catalog(engine.audio, engine.cfg.paths.sounds_catalog);

    render::sys::configure_dialog_style(engine.render, engine.cfg);

    // Dialogue + quests.
    int dialogue_loaded = 0;
    if (!engine.cfg.paths.dialogue_dir.empty())
      dialogue_loaded = engine.graphs.load_all(engine.cfg.paths.dialogue_dir);
    std::println("[engine] Loaded {} dialogue graphs from '{}'", dialogue_loaded, engine.cfg.paths.dialogue_dir);

    int quest_loaded = 0;
    if (!engine.cfg.paths.quests_dir.empty())
      quest_loaded = engine.quests.load_all(engine.cfg.paths.quests_dir);
    std::println("[engine] Loaded {} quests from '{}'", quest_loaded, engine.cfg.paths.quests_dir);
    validate_quest_references(engine.graphs, engine.quests);
    return {};
  }

  namespace {

    // ── Main-loop phases ───────────────────────────────────────────────────────

    /// Result of one frame's fixed-step simulation, consumed by compute_interpolation_alpha().
    struct SimulationResult {
      int steps_run = 0;
      bool entities_deleted = false;
    };

    /// Poll platform input and handle the Quit action.
    void process_input(Engine &engine) noexcept {
      input::sys::poll(engine.input_state, *engine.window);
      if (engine.input_state.is_held(input::Action::Quit))
        request_quit(engine);
    }

    /// Drain the timer accumulator: run gameplay, dialogue events, the
    /// on_fixed_update hook, and deletion flushing once per fixed step.
    [[nodiscard]] SimulationResult run_fixed_steps(Engine &engine) noexcept {
      SimulationResult result;
      while (engine.timer.step()) {
        ++result.steps_run;
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

        // Deletions invalidate the prev_* slot snapshot (swap-and-pop) — see compute_interpolation_alpha().
        result.entities_deleted = result.entities_deleted || engine.scene.world.pending_deletion_count > 0;
        gameplay::entity::flush_deletions(engine.scene.world);

        input::clear_pressed(engine.input_state);
      }
      return result;
    }

    /// Interpolation factor for this frame's render.
    ///
    /// prev_col/prev_row are snapshotted by dense slot before the fixed-step loop
    /// runs. flush_deletions() reassigns slots via swap-and-pop, so after any
    /// deletion a snapshot slot could belong to a different entity than the one
    /// captured — force alpha to 0 rather than interpolate against a stale or
    /// mismatched slot. Alpha is also 0 unless exactly one step ran, so multi-step
    /// (and zero-step) frames render current state.
    [[nodiscard]] float compute_interpolation_alpha(const core::time::LoopTimer &timer, SimulationResult sim) noexcept {
      return (sim.steps_run == 1 && !sim.entities_deleted) ? timer.alpha() : 0.f;
    }

    /// begin_frame → world/UI render → optional debug HUD → end_frame.
    void render_frame(Engine &engine, const float alpha) noexcept {
      engine.renderer->begin_frame(engine.clear_colour);
      render::sys::render(*engine.renderer, engine.render, engine.cfg, engine.scene, engine.flags, alpha, engine.win_w,
                          engine.win_h);

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

    /// World mode: load at most one pending chunk per frame — queues I/O between
    /// frames so chunk-boundary loads don't hitch the render pass.
    void stream_world_chunks(Engine &engine) noexcept {
      if (engine.render.mode == render::data::RenderMode::World)
        render::sys::load_one_pending_chunk(*engine.renderer, engine.render, engine.cfg);
    }

  } // namespace

  void run_loop(Engine &engine) noexcept {
    while (engine.window->is_open() && !engine.quit) {
      std::tie(engine.win_w, engine.win_h) = engine.window->size();
      render::sys::snapshot_prev_frame(engine.render, engine.scene);
      engine.timer.tick();

      process_input(engine);

      const SimulationResult sim = run_fixed_steps(engine);
      if (engine.render.mode != render::data::RenderMode::World)
        gameplay::world::handle_map_transition(engine);

      const float alpha = compute_interpolation_alpha(engine.timer, sim);
      render_frame(engine, alpha);

      stream_world_chunks(engine);
    }
  }

  void cleanup(Engine &engine) noexcept {
    audio::sys::shutdown(engine.audio);
    if (engine.window)
      engine.window->close();
    engine.quit = true;
  }

  void request_quit(Engine &engine) noexcept {
    engine.quit = true;
  }

} // namespace corundum
