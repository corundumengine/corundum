// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/math/isometric.hpp>
#include <corundum/core/math/vec.hpp>
#include <corundum/world/camera.hpp>
#include <corundum/world/map_view.hpp>
#include <corundum/world/picking.hpp>

#include <cmath>
#include <limits>
#include <optional>

namespace corundum::world {

  namespace {

    struct CandidateHit {
      TileCoord coord{};

      float depth{};
    };

    /// The candidate cell the mouse point resolves to at (@p col, @p row), or std::nullopt when
    /// the point does not fall in that cell at its elevation.
    std::optional<CandidateHit> hit_at(const MapView &map, const core::math::Vec2 &world, int col, int row,
                                       const core::math::IsometricParams &iso) noexcept {
      const int elev = discrete_elevation_at(map, col, row);
      const core::math::Vec2 frac = core::math::world_to_tile(world, elev, iso);
      if (static_cast<int>(std::floor(frac.x)) != col || static_cast<int>(std::floor(frac.y)) != row)
        return std::nullopt;

      return CandidateHit{
          .coord = {.col = col, .row = row},
          .depth = core::math::iso_depth_key(static_cast<float>(col), static_cast<float>(row), static_cast<float>(elev),
                                             iso),
      };
    }

    /// world_to_tile's horizontal term is elevation-independent, so a matching cell must sit on the
    /// mouse's diagonal band; the +/-1 margin absorbs float drift near an integer.
    bool on_candidate_diagonal(int col, int row, int u_floor) noexcept {
      const int diagonal = col - row;
      return diagonal >= u_floor - 1 && diagonal <= u_floor + 2;
    }

    /// Highest-depth candidate cell under the mouse, or std::nullopt when none matches.
    std::optional<TileCoord> best_candidate(const MapView &map, const core::math::Vec2 &world,
                                            const core::math::IsometricParams &iso, int u_floor, int grid_w,
                                            int grid_h) noexcept {
      std::optional<TileCoord> best;
      float best_depth = -std::numeric_limits<float>::infinity();
      for (int row = 0; row < grid_h; ++row) {
        for (int col = 0; col < grid_w; ++col) {
          if (!on_candidate_diagonal(col, row, u_floor))
            continue;
          const std::optional<CandidateHit> hit = hit_at(map, world, col, row, iso);
          if (hit && hit->depth > best_depth) {
            best_depth = hit->depth;
            best = hit->coord;
          }
        }
      }
      return best;
    }

  } // namespace

  std::optional<TileCoord> pick_tile(float mouse_x, float mouse_y, const Camera &camera, const MapView &map,
                                     const core::math::IsometricParams &iso) noexcept {
    if (map.elevation_map == nullptr && map.world_area == nullptr)
      return std::nullopt;
    if (camera.zoom <= 0.f || iso.half_tw <= 0.f || iso.half_th <= 0.f)
      return std::nullopt;

    const int grid_w = map.elevation_map != nullptr ? map.elevation_map->width : static_cast<int>(map.world_w_tiles);
    const int grid_h = map.elevation_map != nullptr ? map.elevation_map->height : static_cast<int>(map.world_h_tiles);

    const core::math::Vec2 world{.x = (mouse_x / camera.zoom) + camera.x, .y = (mouse_y / camera.zoom) + camera.y};

    const float u = (world.x - iso.x_origin) / iso.half_tw;
    const int u_floor = static_cast<int>(std::floor(u));

    return best_candidate(map, world, iso, u_floor, grid_w, grid_h);
  }

} // namespace corundum::world
