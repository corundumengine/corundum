// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/math/isometric.hpp>
#include <corundum/core/math/vec.hpp>
#include <corundum/engine.hpp>
#include <corundum/entities/components.hpp>
#include <corundum/render/render_state.hpp>
#include <corundum/render/render_system.hpp>
#include <corundum/world/camera.hpp>
#include <corundum/world/map_view.hpp>
#include <corundum/world/portals/portal.hpp>
#include <corundum/world/spawn.hpp>
#include <corundum/world/tilemap/world_manifest.hpp>
#include <corundum/world/transition.hpp>
#include <corundum/world/world_bounds.hpp>

#include "core/warn_log.hpp"

#include <algorithm>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace corundum::world {

  namespace {

    using corundum::detail::warn_log;

    /// Common failure path: log @p error under @p context, request quit, and close the window.
    void fail(corundum::Engine &engine, std::string_view context, std::string_view error) noexcept {
      warn_log("[engine] {} failed: {}", context, error);
      engine.request_quit();
      engine.window->close();
    }

    /// Zone id for world mode: the manifest's directory name, falling back to the manifest
    /// file stem when it sits directly in the working directory (empty parent).
    std::string world_zone_id(const tilemap::WorldManifest &manifest, std::string_view manifest_path) {
      std::string directory = manifest.base_dir.filename().string();
      if (!directory.empty())
        return directory;
      return std::filesystem::path(manifest_path).stem().string();
    }

  } // namespace

  void frame_camera_on(corundum::Engine &engine, const corundum::core::math::IsometricParams &iso, float col, float row,
                       WorldBounds bounds, CameraAnchor anchor) noexcept {
    engine.scene.camera.zoom = std::clamp(engine.cfg.default_zoom, engine.cfg.min_zoom, engine.cfg.max_zoom);
    const corundum::core::math::Vec2 target = anchor == CameraAnchor::CellCenter
                                                  ? corundum::core::math::tile_to_world_center(col, row, 0.f, iso)
                                                  : corundum::core::math::tile_to_world(col, row, 0, iso);
    const float view_w = engine.window_width() > 0 ? static_cast<float>(engine.window_width()) : engine.cfg.win_w;
    const float view_h = engine.window_height() > 0 ? static_cast<float>(engine.window_height()) : engine.cfg.win_h;
    engine.scene.camera.center_on(target.x, target.y, bounds, view_w, view_h);
  }

  std::expected<void, std::string> enter_world(corundum::Engine &engine,
                                               const corundum::render::WorldLoadParams &params) {
    std::expected<corundum::render::WorldLoadInfo, std::string> world_result =
        render::load_world(*engine.renderer, engine.render, engine.cfg, params);
    if (!world_result)
      return std::unexpected(std::move(world_result).error());
    if (engine.render.chunks.active_empty())
      return std::unexpected("world load produced no active chunks");

    const corundum::render::WorldLoadInfo &info = *world_result;
    const corundum::entities::Position spawn_pos{.col = info.spawn_world_pos.x, .row = info.spawn_world_pos.y};

    std::expected<std::unique_ptr<Scene>, std::string> scene_result =
        world::spawn_world(engine.cfg, engine.characters, engine.render.chunks.active_at(0).tilemap, spawn_pos, false);
    if (!scene_result)
      return std::unexpected(std::move(scene_result).error());
    engine.scene = std::move(**scene_result);
    engine.scene.zone_id = world_zone_id(engine.render.manifest, engine.cfg.paths.world_manifest_path);
    world::sync_chunk_actors(engine.scene, engine.render, engine.cfg, engine.characters);

    const auto [world_width, world_height] =
        corundum::world::tilemap::world_bounds_iso(engine.render.manifest, info.half_tw, info.half_th);
    const WorldBounds bounds{.width_px = world_width, .height_px = world_height};
    // elev_step is pre-multiplied by tile_scale to match compute_isometric_params()
    // for consistency with the renderer's scaled iso.elev_step. Designated
    // initializers keep this robust to field-order changes in IsometricParams.
    const corundum::core::math::IsometricParams iso{
        .half_tw = info.half_tw,
        .half_th = info.half_th,
        .x_origin = info.x_origin,
        .elev_step = engine.cfg.elevation_step_px * engine.cfg.tile_scale,
    };
    frame_camera_on(engine, iso, spawn_pos.col, spawn_pos.row, bounds, CameraAnchor::CellCenter);
    return {};
  }

  void handle_map_transition(corundum::Engine &engine) noexcept {
    try {
      if (!engine.scene.pending_transition)
        return;
      const MapTransition transition = std::move(*engine.scene.pending_transition);
      engine.scene.pending_transition.reset();

      if (transition.return_to_world) {
        // Re-enter the overworld, positioned at the exit portal's spawn tile (the exit
        // portal always carries one). A World→interior journey sets the marker; entering
        // an interior without ever leaving a world leaves it clear.
        const bool can_return{engine.entered_from_world || !engine.cfg.paths.world_manifest_path.empty()};
        if (!can_return) {
          // No overworld was ever loaded (single-map config with an exit portal): there is
          // nowhere to return to, so ignore the portal instead of terminating the game.
          warn_log("[engine] return_to_world portal traversed with no overworld configured; ignoring");
          return;
        }
        engine.entered_from_world = false;
        if (auto result = apply_spawn(engine, SpawnMode::World, "", "", static_cast<float>(transition.spawn_col),
                                      static_cast<float>(transition.spawn_row));
            !result)
          fail(engine, "return-to-world", result.error());
        return;
      }

      // Cross-map (enter-interior) transition. Mark the journey only when actually leaving
      // the World — an interior→interior (nested) cross-map transition must not overwrite it.
      if (engine.render.mode == render::RenderMode::World)
        engine.entered_from_world = true;

      if (auto result = apply_spawn(engine, SpawnMode::SingleMap, transition.target_map, "",
                                    static_cast<float>(transition.spawn_col), static_cast<float>(transition.spawn_row));
          !result)
        fail(engine, "map transition", result.error());
    } catch (...) {
      // An allocation/format failure cannot escape a noexcept main-loop call; quit rather
      // than continue with a half-applied scene transition. Clear the journey marker so a
      // later load does not inherit a journey that never completed.
      engine.entered_from_world = false;
      fail(engine, "map transition", "unexpected exception");
    }
  }

  std::expected<void, std::string> apply_spawn(corundum::Engine &engine, SpawnMode mode, std::string_view id,
                                               std::string_view zone, float col, float row) {
    if (mode == SpawnMode::World) {
      corundum::render::WorldLoadParams params;
      params.spawn_col = col;
      params.spawn_row = row;
      if (auto result = enter_world(engine, params); !result)
        return std::unexpected(std::move(result).error());
      if (!zone.empty())
        engine.scene.zone_id = std::string(zone);
      return {};
    }

    std::expected<void, std::string> map_result =
        render::load_map(*engine.renderer, engine.render, std::string(id), engine.cfg);
    if (!map_result)
      return std::unexpected(std::move(map_result).error());

    const corundum::world::tilemap::Tilemap &new_tilemap = *engine.active_tilemap();
    const entities::Position spawn{.col = col, .row = row};
    std::expected<std::unique_ptr<Scene>, std::string> scene_result =
        world::spawn_world(engine.cfg, engine.characters, new_tilemap, spawn);
    if (!scene_result)
      return std::unexpected(std::move(scene_result).error());
    engine.scene = std::move(**scene_result);
    if (!zone.empty())
      engine.scene.zone_id = std::string(zone);

    // Match the world's camera framing for the interior: centre on the entry tile, mirroring
    // the initial single-map boot and enter_world. The historical cross-map path uses the
    // top-vertex anchor (tile_to_world); preserve that behaviour.
    const corundum::core::math::IsometricParams iso = corundum::core::math::compute_isometric_params(
        new_tilemap.diamond_w(), new_tilemap.diamond_h(), new_tilemap.height, engine.cfg.tile_scale,
        engine.cfg.elevation_step_px);
    const WorldBounds bounds{single_map_bounds(new_tilemap, iso.half_tw, iso.half_th)};
    frame_camera_on(engine, iso, spawn.col, spawn.row, bounds, CameraAnchor::TopVertex);
    return {};
  }

} // namespace corundum::world
