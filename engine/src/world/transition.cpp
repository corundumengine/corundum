#include <corundum/engine.hpp>
#include <corundum/entities/components.hpp>
#include <corundum/render/render_sys.hpp>
#include <corundum/world/camera.hpp>
#include <corundum/world/spawn.hpp>
#include <corundum/world/tilemap/world_manifest.hpp>
#include <corundum/world/transition.hpp>

#include <algorithm>
#include <filesystem>
#include <print>
#include <string_view>
#include <utility>

namespace corundum::world {

  namespace {

    /// @brief Owning type for scene-transition behaviour (overworld boot, interior entry,
    ///        return-to-world).
    ///
    /// The free functions @ref enter_world and @ref handle_map_transition were a cluster
    /// of loose helpers with the same camera-framing, viewport-resolution, and
    /// failure-reporting logic repeated across both branches (and again in
    /// @c engine.cpp::InitPipeline). This class consolidates those shared helpers into
    /// one cohesive owner; it carries no state of its own, only the @c Engine reference
    /// that is the subject under transition.
    class SceneTransitioner {
    public:
      explicit SceneTransitioner(corundum::Engine &engine) noexcept : engine_(engine) {}

      /// @brief (Re)initialise the overworld scene, optionally at a specific spawn tile.
      [[nodiscard]] std::expected<void, std::string> enter_world(const corundum::render::WorldLoadParams &params);

      /// @brief Handle a pending map transition triggered by portal traversal.
      void handle_map_transition() noexcept;

      /// @brief Rebuild the scene for a saved or transitioned location (see world::apply_spawn).
      [[nodiscard]] std::expected<void, std::string> apply_spawn(std::string_view mode, std::string_view id,
                                                                 std::string_view zone, float col, float row) noexcept;

    private:
      /// @brief Live window size when populated (mid-frame), falling back to the
      ///        configured boot-time size before @c run_frame() has populated
      ///        @c Engine::win_w/@c Engine::win_h.
      [[nodiscard]] std::pair<float, float> viewport() const noexcept;

      /// @brief Clamp @c GameConfig::default_zoom into [min_zoom, max_zoom] and
      ///        write the result to @c scene.camera.zoom.
      void apply_default_zoom() noexcept;

      /// @brief Apply default zoom, project (@p col, @p row) into world space via
      ///        @p iso, and centre the camera on the projected point clamped to
      ///        the world bounds.
      ///
      /// @param anchor_cell_center  @c true uses @c tile_to_world_center (cell-centre
      ///        anchor — the world-load path keeps the camera tracking the player
      ///        exactly); @c false uses @c tile_to_world (top-vertex anchor — the
      ///        cross-map path preserves its historical projection).
      void frame_camera_on(const corundum::core::math::IsometricParams &iso, float col, float row, float world_w,
                           float world_h, bool anchor_cell_center) noexcept;

      /// @brief Common failure path: log @p err under @p context label, request
      ///        quit, and close the window.
      void fail(std::string_view context, std::string_view err) noexcept;

      corundum::Engine &engine_;
    };

    std::pair<float, float> SceneTransitioner::viewport() const noexcept {
      const float vw = engine_.win_w > 0 ? static_cast<float>(engine_.win_w) : static_cast<float>(engine_.cfg.win_w);
      const float vh = engine_.win_h > 0 ? static_cast<float>(engine_.win_h) : static_cast<float>(engine_.cfg.win_h);
      return {vw, vh};
    }

    void SceneTransitioner::apply_default_zoom() noexcept {
      engine_.scene.camera.zoom = std::clamp(engine_.cfg.default_zoom, engine_.cfg.min_zoom, engine_.cfg.max_zoom);
    }

    void SceneTransitioner::frame_camera_on(const corundum::core::math::IsometricParams &iso, float col, float row,
                                            float world_w, float world_h, bool anchor_cell_center) noexcept {
      apply_default_zoom();
      const auto target = anchor_cell_center ? corundum::core::math::tile_to_world_center(col, row, 0.f, iso)
                                             : corundum::core::math::tile_to_world(col, row, 0, iso);
      const auto [vw, vh] = viewport();
      engine_.scene.camera.center_on(target.x, target.y, world_w, world_h, vw, vh);
    }

    void SceneTransitioner::fail(std::string_view context, std::string_view err) noexcept {
      std::println(stderr, "[engine] {} failed: {}", context, err);
      request_quit(engine_);
      engine_.window->close();
    }

    std::expected<void, std::string> SceneTransitioner::enter_world(const corundum::render::WorldLoadParams &params) {
      auto world_result = render::load_world(*engine_.renderer, engine_.render, engine_.cfg, params);
      if (!world_result)
        return std::unexpected(std::move(world_result).error());
      const auto &info = *world_result;
      const corundum::entities::Position spawn_pos{info.spawn_world_pos.x, info.spawn_world_pos.y};

      auto scene_result = world::spawn_world(engine_.cfg, engine_.characters,
                                             engine_.render.chunks.active_at(0).tilemap, spawn_pos, false);
      if (!scene_result)
        return std::unexpected(std::move(scene_result).error());
      engine_.scene = std::move(*scene_result);
      // World mode: the zone is the world itself, keyed by its manifest directory
      // name (spawn_world above keyed it off the chunk tilemap stem).
      engine_.scene.zone_id =
          std::filesystem::path(engine_.cfg.paths.world_manifest_path).parent_path().filename().string();
      world::sync_chunk_actors(engine_.scene, engine_.render, engine_.cfg, engine_.characters);

      const auto [world_width, world_height] =
          corundum::world::tilemap::world_bounds_iso(engine_.render.manifest, info.half_tw, info.half_th);
      // elev_step is pre-multiplied by tile_scale to match compute_isometric_params()
      // for consistency with the renderer's scaled iso.elev_step. Designated
      // initializers keep this robust to field-order changes in IsometricParams.
      const corundum::core::math::IsometricParams iso{
          .half_tw = info.half_tw,
          .half_th = info.half_th,
          .x_origin = info.x_origin,
          .elev_step = engine_.cfg.elevation_step_px * engine_.cfg.tile_scale,
      };
      frame_camera_on(iso, spawn_pos.col, spawn_pos.row, world_width, world_height, /*anchor_cell_center=*/true);
      return {};
    }

    void SceneTransitioner::handle_map_transition() noexcept {
      if (!engine_.scene.pending_transition)
        return;
      const auto t = *engine_.scene.pending_transition;
      engine_.scene.pending_transition.reset();

      if (t.return_to_world) {
        // Re-enter the overworld, positioned at the exit portal's spawn tile (the
        // exit portal always carries one). A World→interior journey sets the marker;
        // entering an interior without ever leaving a world leaves it clear.
        const bool can_return = engine_.entered_from_world || !engine_.cfg.paths.world_manifest_path.empty();
        if (can_return) {
          engine_.entered_from_world = false;
          if (auto result =
                  apply_spawn("world", "", "", static_cast<float>(t.spawn_col), static_cast<float>(t.spawn_row));
              !result)
            fail("return-to-world", result.error());
        } else {
          // No overworld was ever loaded (single-map config with an exit portal):
          // there is nowhere to return to, so ignore the portal instead of
          // terminating the game.
          std::println(stderr, "[engine] return_to_world portal traversed with no overworld configured; ignoring");
        }
        return;
      }

      // Cross-map (enter-interior) transition. Mark the journey only when actually
      // leaving the World — an interior→interior (nested) cross-map transition
      // must not overwrite it.
      if (engine_.render.mode == render::RenderMode::World)
        engine_.entered_from_world = true;

      if (auto result = apply_spawn("single_map", t.target_map, "", static_cast<float>(t.spawn_col),
                                    static_cast<float>(t.spawn_row));
          !result)
        fail("map transition", result.error());
    }

    std::expected<void, std::string> SceneTransitioner::apply_spawn(std::string_view mode, std::string_view id,
                                                                    std::string_view zone, float col,
                                                                    float row) noexcept {
      if (mode == "world") {
        corundum::render::WorldLoadParams params;
        params.spawn_col = static_cast<int>(col);
        params.spawn_row = static_cast<int>(row);
        if (auto result = enter_world(params); !result)
          return std::unexpected(std::move(result).error());
        if (!zone.empty())
          engine_.scene.zone_id = std::string(zone);
        return {};
      }

      auto map_result = render::load_map(*engine_.renderer, engine_.render, std::string(id), engine_.cfg);
      if (!map_result)
        return std::unexpected(std::move(map_result).error());

      const auto &new_tm = *active_tilemap(engine_);
      const entities::Position spawn{.col = col, .row = row};
      auto scene_result = world::spawn_world(engine_.cfg, engine_.characters, new_tm, spawn);
      if (!scene_result)
        return std::unexpected(std::move(scene_result).error());
      engine_.scene = std::move(*scene_result);
      if (!zone.empty())
        engine_.scene.zone_id = std::string(zone);

      // Match the world's camera framing for the interior: centre on the entry tile,
      // mirroring init_single_map_scene / enter_world. The historical cross-map
      // path uses the top-vertex anchor (tile_to_world); preserve that behaviour.
      const auto iso = corundum::core::math::compute_isometric_params(
          new_tm.diamond_w(), new_tm.diamond_h(), new_tm.height, engine_.cfg.tile_scale, engine_.cfg.elevation_step_px);
      const float map_extent = static_cast<float>(new_tm.width + new_tm.height - 1) * iso.half_tw * 2.f;
      frame_camera_on(iso, spawn.col, spawn.row, map_extent, map_extent, /*anchor_cell_center=*/false);
      return {};
    }

  } // namespace

  std::expected<void, std::string> enter_world(corundum::Engine &engine,
                                               const corundum::render::WorldLoadParams &params) {
    return SceneTransitioner{engine}.enter_world(params);
  }

  std::expected<void, std::string> apply_spawn(corundum::Engine &engine, std::string_view mode, std::string_view id,
                                               std::string_view zone, float col, float row) noexcept {
    return SceneTransitioner{engine}.apply_spawn(mode, id, zone, col, row);
  }

  void handle_map_transition(corundum::Engine &engine) noexcept {
    SceneTransitioner{engine}.handle_map_transition();
  }

} // namespace corundum::world
