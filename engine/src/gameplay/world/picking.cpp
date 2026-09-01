#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/world/picking.hpp>
#include <corundum/gameplay/world/tilemap/tilemap.hpp>
#include <corundum/gameplay/world/update.hpp>
#include <corundum/render/render_sys.hpp>

#include <cmath>
#include <limits>

namespace corundum::gameplay::world {

  namespace {

    /// Resolve the discrete tile elevation at (col, row) for the active MapView source.
    /// Picks the world-mode path when both elevation sources are set (matches the original
    /// world_mode-takes-priority convention). Caller must guarantee at least one source is
    /// non-null — pick_tile's guard checks that first.
    [[nodiscard]] int pick_elevation_at(const corundum::gameplay::world::MapView &map, int col, int row) noexcept {
      if (map.world_render != nullptr)
        return static_cast<int>(std::lround(
            corundum::render::elevation_under(*map.world_render, static_cast<float>(col), static_cast<float>(row))));
      return corundum::gameplay::world::tilemap::elevation_at(*map.elevation_map, col, row);
    }

  } // namespace

  std::optional<TileCoord> pick_tile(float mouse_x, float mouse_y, const corundum::gameplay::world::Camera &camera,
                                     const corundum::gameplay::world::MapView &map, float elev_step,
                                     float zoom) noexcept {
    if (map.elevation_map == nullptr && map.world_render == nullptr)
      return std::nullopt;

    const int grid_w = map.elevation_map ? map.elevation_map->width : static_cast<int>(map.world_w_tiles);
    const int grid_h = map.elevation_map ? map.elevation_map->height : static_cast<int>(map.world_h_tiles);

    const corundum::core::math::Vec2 world{mouse_x / zoom + camera.x, mouse_y / zoom + camera.y};

    std::optional<TileCoord> best;
    float best_depth = -std::numeric_limits<float>::infinity();

    for (int row = 0; row < grid_h; ++row) {
      for (int col = 0; col < grid_w; ++col) {
        const int elev = pick_elevation_at(map, col, row);
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

} // namespace corundum::gameplay::world
