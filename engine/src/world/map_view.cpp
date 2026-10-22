// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/game_config.hpp>
#include <corundum/core/math/isometric.hpp>
#include <corundum/render/render_state.hpp>
#include <corundum/render/world_aggregates.hpp>
#include <corundum/world/map_view.hpp>
#include <corundum/world/tilemap/tilemap.hpp>
#include <corundum/world/tilemap/world_manifest.hpp>
#include <corundum/world/world_bounds.hpp>

#include <cmath>
#include <span>
#include <variant>

namespace corundum::world {

  namespace {

    /// MapView for a single map.
    [[nodiscard]] MapView build_single_map_view(const render::SingleMapArea &area,
                                                const core::GameConfig &cfg) noexcept {
      const tilemap::Tilemap &tm = area.map.tilemap;
      const core::math::IsometricParams isometric = render::isometric_params(area, cfg);
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
          .portals = area.map.portals,
          .elevation_map = &tm,
          .walkability = &area.walkability,
      };
    }

    /// MapView for a streamed world.
    /// @pre @p area has at least one active chunk and its aggregates are rebuilt.
    [[nodiscard]] MapView build_world_map_view(const render::WorldArea &area, const core::GameConfig &cfg) noexcept {
      const tilemap::WorldManifest &manifest = area.manifest;
      const int total_h{manifest.effective_tiles_tall()};
      const core::math::IsometricParams isometric = render::isometric_params(area, cfg);
      const auto [iso_w, iso_h] = tilemap::world_bounds_iso(manifest, isometric.half_tw, isometric.half_th);
      return {
          .collisions = area.aggregates.collisions.view(),
          .collision_triangles = area.aggregates.triangles.view(),
          .world_w_px = iso_w,
          .world_h_px = iso_h,
          .world_w_tiles = static_cast<float>(manifest.effective_tiles_wide()),
          .world_h_tiles = static_cast<float>(total_h),
          .half_tw = isometric.half_tw,
          .half_th = isometric.half_th,
          .x_origin = isometric.x_origin,
          .character_scale = cfg.character_scale,
          .tile_scale = cfg.tile_scale,
          .portals = std::span{area.aggregates.portals},
          .walkability = &area.aggregates.walkability,
          .world_area = &area,
      };
    }

    /// With no area loaded, fixed steps still run (see the fixed-step loop in engine.cpp) and see
    /// an empty single map, as they did before RenderState::area became a variant.
    [[nodiscard]] const render::SingleMapArea &no_area() noexcept {
      static const render::SingleMapArea k_empty{};
      return k_empty;
    }

  } // namespace

  [[nodiscard]] MapView build_map_view(const render::RenderState &render, const core::GameConfig &cfg) noexcept {
    const auto *world_area = std::get_if<render::WorldArea>(&render.area);
    if (world_area != nullptr && !world_area->chunks.active_empty())
      return build_world_map_view(*world_area, cfg);
    if (const auto *single = std::get_if<render::SingleMapArea>(&render.area))
      return build_single_map_view(*single, cfg);
    return build_single_map_view(no_area(), cfg);
  }

  WorldBounds single_map_bounds(const tilemap::Tilemap &tm, float half_tw, float half_th) noexcept {
    return tilemap::world_bounds_for_tiles(tm.width, tm.height, half_tw, half_th);
  }

  float elevation_at_tile(const MapView &map, float col_f, float row_f) noexcept {
    if (map.elevation_map != nullptr)
      return corundum::world::tilemap::interpolated_elevation_at(*map.elevation_map, col_f, row_f);
    if (map.world_area != nullptr)
      return render::elevation_under(*map.world_area, col_f, row_f);
    return 0.f;
  }

  int discrete_elevation_at(const MapView &map, int col, int row) noexcept {
    if (map.elevation_map != nullptr)
      return corundum::world::tilemap::elevation_at(*map.elevation_map, col, row);
    if (map.world_area != nullptr)
      return static_cast<int>(
          std::lround(render::elevation_under(*map.world_area, static_cast<float>(col), static_cast<float>(row))));
    return 0;
  }

} // namespace corundum::world
