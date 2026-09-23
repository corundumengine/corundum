// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/game_config.hpp>
#include <corundum/core/math/isometric.hpp>
#include <corundum/render/render_state.hpp>
#include <corundum/render/render_system.hpp>
#include <corundum/world/map_view.hpp>
#include <corundum/world/tilemap/tilemap.hpp>
#include <corundum/world/tilemap/world_manifest.hpp>
#include <corundum/world/world_bounds.hpp>

#include <cmath>
#include <span>

namespace corundum::world {

  namespace {

    /// MapView for the single-tilemap render mode.
    [[nodiscard]] MapView build_single_map_view(const render::RenderState &render,
                                                const core::GameConfig &cfg) noexcept {
      const tilemap::Tilemap &tm = render.map_data.tilemap;
      const core::math::IsometricParams isometric = core::math::compute_isometric_params(
          tm.diamond_w(), tm.diamond_h(), tm.height, cfg.tile_scale, cfg.elevation_step_px);
      const WorldBounds bounds{single_map_bounds(tm, isometric.half_tw, isometric.half_th)};
      return {
          .collisions = tm.collisions.view(),
          .collision_triangles = tm.collision_triangles.view(),
          .world_w_px = bounds.width_px,
          .world_h_px = bounds.height_px,
          .world_w_tiles = static_cast<float>(tm.width),
          .world_h_tiles = static_cast<float>(tm.height),
          .half_tw = isometric.half_tw,
          .half_th = isometric.half_th,
          .x_origin = isometric.x_origin,
          .character_scale = cfg.character_scale,
          .tile_scale = cfg.tile_scale,
          .portals = render.map_data.portals,
          .elevation_map = &tm,
          .walkability = &render.map_walkability,
      };
    }

    /// MapView for the streamed chunk-world render mode.
    ///
    /// @pre @p render has at least one active chunk and every active chunk shares the same
    ///      diamond size; the world aggregates have already been rebuilt.
    [[nodiscard]] MapView build_world_map_view(const render::RenderState &render,
                                               const core::GameConfig &cfg) noexcept {
      const tilemap::Tilemap &first_tm = render.chunks.active_at(0).tilemap;
      const tilemap::WorldManifest &manifest = render.manifest;
      const int total_h{manifest.effective_tiles_tall()};
      const core::math::IsometricParams isometric = core::math::compute_isometric_params(
          first_tm.diamond_w(), first_tm.diamond_h(), total_h, cfg.tile_scale, cfg.elevation_step_px);
      const auto [iso_w, iso_h] = tilemap::world_bounds_iso(manifest, isometric.half_tw, isometric.half_th);

      return {
          .collisions = render.agg_collisions.view(),
          .collision_triangles = render.agg_triangles.view(),
          .world_w_px = iso_w,
          .world_h_px = iso_h,
          .world_w_tiles = static_cast<float>(manifest.effective_tiles_wide()),
          .world_h_tiles = static_cast<float>(total_h),
          .half_tw = isometric.half_tw,
          .half_th = isometric.half_th,
          .x_origin = isometric.x_origin,
          .character_scale = cfg.character_scale,
          .tile_scale = cfg.tile_scale,
          .portals = std::span{render.agg_portals},
          .walkability = &render.agg_walkability,
          .world_render = &render,
      };
    }

  } // namespace

  [[nodiscard]] MapView build_map_view(const render::RenderState &render, const core::GameConfig &cfg) noexcept {
    if (render.mode == render::RenderMode::World)
      return build_world_map_view(render, cfg);
    return build_single_map_view(render, cfg);
  }

  WorldBounds single_map_bounds(const tilemap::Tilemap &tm, float half_tw, float half_th) noexcept {
    return tilemap::world_bounds_for_tiles(tm.width, tm.height, half_tw, half_th);
  }

  float elevation_at_tile(const MapView &map, float col_f, float row_f) noexcept {
    if (map.elevation_map != nullptr)
      return corundum::world::tilemap::interpolated_elevation_at(*map.elevation_map, col_f, row_f);
    if (map.world_render != nullptr)
      return render::elevation_under(*map.world_render, col_f, row_f);
    return 0.f;
  }

  int discrete_elevation_at(const MapView &map, int col, int row) noexcept {
    if (map.elevation_map != nullptr)
      return corundum::world::tilemap::elevation_at(*map.elevation_map, col, row);
    if (map.world_render != nullptr)
      return static_cast<int>(
          std::lround(render::elevation_under(*map.world_render, static_cast<float>(col), static_cast<float>(row))));
    return 0;
  }

} // namespace corundum::world
