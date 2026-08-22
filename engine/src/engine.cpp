#include <corundum/debug/debug_overlay.hpp>
#include <corundum/engine.hpp>
#include <corundum/gameplay/dialogue/validate_refs.hpp>
#include <corundum/gameplay/entity/world.hpp>
#include <corundum/gameplay/quest/runner.hpp>
#include <corundum/gameplay/quest/system.hpp>
#include <corundum/gameplay/sys/camera_system.hpp>
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

    void validate_quest_references(const corundum::gameplay::dialogue::Registry &graphs,
                                   const corundum::gameplay::quest::Registry &quests) {
      for (const auto &[id, graph] : graphs) {
        for (const auto &err : corundum::gameplay::dialogue::validate_quest_refs(graph, quests))
          std::println(stderr, "[engine] WARN: dialogue '{}' {}", id, err);
        for (const auto &err : corundum::gameplay::dialogue::validate_condition_quest_refs(graph, quests))
          std::println(stderr, "[engine] WARN: dialogue '{}' {}", id, err);
      }
    }

    /// Owns the initialize() sequence as private, ordered phases. The only
    /// public entry point is run() — phase order is therefore structural
    /// (nothing outside this class can call a phase out of order), not a
    /// convention enforced by statement order in one function.
    class InitPipeline {
    public:
      explicit InitPipeline(Engine &engine) : engine_(engine) {}

      std::expected<void, std::string> run(core::GameConfig &&cfg) {
        engine_.cfg = std::move(cfg);
        engine_.timer.set_target_fps(static_cast<float>(engine_.cfg.framerate));
        engine_.window->set_vsync(engine_.cfg.vsync);

        if (auto result = load_render_assets(); !result)
          return result;

        if (auto result = init_scene(); !result)
          return result;

        init_audio();
        render::sys::configure_dialog_style(engine_.render, engine_.cfg);
        load_dialogue_and_quests();
        return {};
      }

    private:
      std::expected<void, std::string> load_render_assets() {
        auto char_result = engine_.characters.load_all(engine_.cfg.paths.sprites_dir);
        if (!char_result)
          return std::unexpected(char_result.error());
        render::sys::load_sprite_index(*engine_.renderer, engine_.render, engine_.characters);

        const auto font_path = std::format("{}/{}", engine_.cfg.paths.font_dir, engine_.cfg.paths.game_font);
        auto font_result = render::sys::load_font(*engine_.renderer, engine_.render, font_path);
        if (!font_result)
          return std::unexpected(font_result.error());

        auto ui_result = render::sys::load_ui_assets(*engine_.renderer, engine_.render);
        if (!ui_result)
          return std::unexpected(ui_result.error());
        return {};
      }

      std::expected<void, std::string> init_scene() {
        const bool world_mode = !engine_.cfg.paths.world_manifest_path.empty();
        return world_mode ? init_world_scene() : init_single_map_scene();
      }

      std::expected<void, std::string> init_world_scene() {
        auto world_result = render::sys::load_world(*engine_.renderer, engine_.render, engine_.cfg);
        if (!world_result)
          return std::unexpected(world_result.error());
        const auto &info = *world_result;
        const gameplay::component::Position spawn_pos{info.spawn_world_pos.x, info.spawn_world_pos.y};
        auto scene_result = gameplay::world::spawn_world(engine_.cfg, engine_.characters,
                                                         engine_.render.chunks.active_at(0).tilemap, spawn_pos);
        if (!scene_result)
          return std::unexpected(scene_result.error());
        engine_.scene = std::move(*scene_result);
        const auto [world_width, world_height] =
            gameplay::world::tilemap::world_bounds_iso(engine_.render.manifest, info.half_tw, info.half_th);

        // Convert tile-grid spawn position to isometric for camera tracking.
        // Cell-center anchor (matches entity sprite position) keeps the camera
        // aligned with the player instead of offset half_th above them.
        const corundum::core::math::IsometricParams iso{info.half_tw, info.half_th, info.x_origin,
                                                        engine_.cfg.elevation_step_px};
        const auto [iso_x, iso_y] =
            corundum::core::math::tile_to_world_center(spawn_pos.col, spawn_pos.row, 0.f, iso);

        apply_default_zoom_and_center(iso_x, iso_y, world_width, world_height);
        return {};
      }

      std::expected<void, std::string> init_single_map_scene() {
        auto map_result =
            render::sys::load_map(*engine_.renderer, engine_.render, engine_.cfg.paths.tilemap_path, engine_.cfg);
        if (!map_result)
          return std::unexpected(map_result.error());
        auto scene_result = gameplay::world::spawn_world(engine_.cfg, engine_.characters, *active_tilemap(engine_));
        if (!scene_result)
          return std::unexpected(scene_result.error());
        engine_.scene = std::move(*scene_result);

        const auto &tilemap = *active_tilemap(engine_);
        const auto iso = core::math::compute_isometric_params(tilemap.diamond_w(), tilemap.diamond_h(), tilemap.height,
                                                              engine_.cfg.tile_scale, engine_.cfg.elevation_step_px);
        const auto p_slot = engine_.scene.world.transforms.dense_idx(engine_.scene.player);
        const float player_col = engine_.scene.world.transforms.col[p_slot];
        const float player_row = engine_.scene.world.transforms.row[p_slot];
        const auto iso_pos = core::math::tile_to_world(player_col, player_row, 0.f, iso);

        const float map_extent = static_cast<float>(tilemap.width + tilemap.height - 1) * iso.half_tw * 2.f;

        apply_default_zoom_and_center(iso_pos.x, iso_pos.y, map_extent, map_extent);
        return {};
      }

      /// Set the configured default zoom, then center the camera on the target
      /// point via the shared sys::center_on (formerly duplicated inline in
      /// init_world/init_single_map).
      void apply_default_zoom_and_center(float target_x, float target_y, float world_w, float world_h) {
        engine_.scene.camera.zoom =
            std::clamp(engine_.cfg.default_zoom, engine_.cfg.min_zoom, engine_.cfg.max_zoom);
        gameplay::sys::center_on(engine_.scene.camera, target_x, target_y, world_w, world_h,
                                 static_cast<float>(engine_.cfg.win_w), static_cast<float>(engine_.cfg.win_h));
      }

      void init_audio() {
        std::expected<void, std::string> audio_result = engine_.audio.initialize(engine_.cfg.paths.sounds_dir);
        if (!audio_result)
          std::println("[engine] WARN: Audio init failed — {}", audio_result.error());
        else
          engine_.audio.load_catalog(engine_.cfg.paths.sounds_catalog);
      }

      void load_dialogue_and_quests() {
        int dialogue_loaded = 0;
        if (!engine_.cfg.paths.dialogue_dir.empty())
          dialogue_loaded = engine_.graphs.load_all(engine_.cfg.paths.dialogue_dir);
        std::println("[engine] Loaded {} dialogue graphs from '{}'", dialogue_loaded, engine_.cfg.paths.dialogue_dir);

        int quest_loaded = 0;
        if (!engine_.cfg.paths.quests_dir.empty())
          quest_loaded = engine_.quests.load_all(engine_.cfg.paths.quests_dir);
        std::println("[engine] Loaded {} quests from '{}'", quest_loaded, engine_.cfg.paths.quests_dir);

        validate_quest_references(engine_.graphs, engine_.quests);
      }

      Engine &engine_;
    };

  } // namespace

  void process_dialogue_events(Engine &engine) noexcept {
    gameplay::quest::Runner quest_runner{engine.quests, engine.flags};
    for (const auto &ev : engine.scene.pending_dialogue_events) {
      if (ev.name == "play_sound" && !ev.args.empty()) {
        const auto result = engine.audio.play_sound(ev.args[0]);
        if (!result)
          std::println(stderr, "[engine] WARN: {}", result.error());
      } else if (ev.name == "quest_start" && !ev.args.empty()) {
        if (auto result = quest_runner.start(ev.args[0]); !result)
          std::println(stderr, "[engine] WARN: {}", result.error());
      } else if (ev.name == "quest_advance" && ev.args.size() >= 2) {
        if (auto result = quest_runner.advance(ev.args[0], ev.args[1]); !result)
          std::println(stderr, "[engine] WARN: {}", result.error());
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
    if (!engine.window || !engine.renderer)
      return std::unexpected("initialize: engine.window and engine.renderer must be non-null "
                             "(use make_engine(), or adopt a platform before calling)");

    InitPipeline pipeline(engine);
    if (auto result = pipeline.run(std::move(cfg)); !result) {
      cleanup(engine);
      return std::unexpected(result.error());
    }
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
        if (engine.render.mode == render::data::RenderMode::World && engine.render.chunks.active_empty())
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

      if (engine.hud.enabled) {
        const debug::OverlayInput hud_input{
            .render_state = engine.render,
            .cfg = engine.cfg,
            .scene = engine.scene,
            .timer = engine.timer,
        };
        debug::draw_overlays(*engine.renderer, hud_input, engine.hud);
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
    while (run_frame(engine)) {
    }
  }

  bool run_frame(Engine &engine) noexcept {
    if (!engine.window->is_open() || engine.quit)
      return false;
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
    return true;
  }

  void cleanup(Engine &engine) noexcept {
    engine.audio.shutdown();
    if (engine.window)
      engine.window->close();
    engine.quit = true;
  }

  void request_quit(Engine &engine) noexcept {
    engine.quit = true;
  }

} // namespace corundum
