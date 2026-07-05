#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/sys/picking.hpp>
#include <corundum/gameplay/world/tilemap/tilemap.hpp>
#include <corundum/gameplay/world/update.hpp>

#include <cmath>
#include <limits>

namespace corundum::gameplay::sys {

  std::optional<TileCoord> pick_tile(float mouse_x, float mouse_y, const corundum::gameplay::world::Camera &camera,
                                     const corundum::gameplay::world::MapView &map, float elev_step,
                                     float zoom) noexcept {
    if (!map.elevation_map)
      return std::nullopt;

    const corundum::core::math::Vec2 world{mouse_x / zoom + camera.x, mouse_y / zoom + camera.y};

    std::optional<TileCoord> best;
    float best_depth = -std::numeric_limits<float>::infinity();

    for (int row = 0; row < map.elevation_map->height; ++row) {
      for (int col = 0; col < map.elevation_map->width; ++col) {
        const int elev = corundum::gameplay::world::tilemap::elevation_at(*map.elevation_map, col, row);
        const corundum::core::math::Vec2 frac =
            corundum::core::math::world_to_tile(world, elev, map.half_tw, map.half_th, elev_step, map.x_origin);
        if (static_cast<int>(std::floor(frac.x)) != col || static_cast<int>(std::floor(frac.y)) != row)
          continue;

        const float depth = corundum::core::math::iso_depth_key(static_cast<float>(col), static_cast<float>(row),
                                                                static_cast<float>(elev), map.half_th, elev_step);
        if (depth > best_depth) {
          best_depth = depth;
          best = TileCoord{col, row};
        }
      }
    }

    return best;
  }

} // namespace corundum::gameplay::sys
