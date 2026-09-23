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

  std::optional<TileCoord> pick_tile(float mouse_x, float mouse_y, const Camera &camera, const MapView &map,
                                     const core::math::IsometricParams &iso) noexcept {
    if (map.elevation_map == nullptr && map.world_render == nullptr)
      return std::nullopt;
    if (camera.zoom <= 0.f || iso.half_tw <= 0.f || iso.half_th <= 0.f)
      return std::nullopt;

    const int grid_w = map.elevation_map != nullptr ? map.elevation_map->width : static_cast<int>(map.world_w_tiles);
    const int grid_h = map.elevation_map != nullptr ? map.elevation_map->height : static_cast<int>(map.world_h_tiles);

    const core::math::Vec2 world{.x = (mouse_x / camera.zoom) + camera.x, .y = (mouse_y / camera.zoom) + camera.y};

    // world_to_tile's horizontal term is elevation-independent, so a matching cell must sit
    // on diagonal col - row == floor(u) or floor(u) + 1; the +/-1 margin absorbs float drift
    // near an integer. This keeps the per-cell elevation lookup O(grid) instead of O(grid^2).
    const float u = (world.x - iso.x_origin) / iso.half_tw;
    const int u_floor = static_cast<int>(std::floor(u));

    std::optional<TileCoord> best;
    float best_depth = -std::numeric_limits<float>::infinity();

    for (int row = 0; row < grid_h; ++row) {
      for (int col = 0; col < grid_w; ++col) {
        const int diagonal = col - row;
        if (diagonal < u_floor - 1 || diagonal > u_floor + 2)
          continue;

        const int elev = discrete_elevation_at(map, col, row);
        const core::math::Vec2 frac = core::math::world_to_tile(world, elev, iso);
        if (static_cast<int>(std::floor(frac.x)) != col || static_cast<int>(std::floor(frac.y)) != row)
          continue;

        const float depth =
            core::math::iso_depth_key(static_cast<float>(col), static_cast<float>(row), static_cast<float>(elev), iso);
        if (depth > best_depth) {
          best_depth = depth;
          best = TileCoord{.col = col, .row = row};
        }
      }
    }

    return best;
  }

} // namespace corundum::world
