#pragma once
#include <corundum/gameplay/world/camera.hpp>

#include <optional>

namespace corundum::gameplay::world {
  struct MapView; // update.hpp — forward-declared to avoid scene.hpp -> picking.hpp -> update.hpp -> scene.hpp cycle.
}

namespace corundum::gameplay::sys {

  /**
   * @brief A tile-grid coordinate.
   *
   * Distinct from tools::tilemap::TileCoord (Tilesmith is a separate codebase); this
   * is the engine-side equivalent for picking results.
   */
  struct TileCoord {
    int col;
    int row;
  };

  /**
   * @brief Convert a mouse position to the tile it's over, elevation-aware.
   *
   * @details Single-map mode only (returns nullopt if @p map.elevation_map is null,
   * matching the existing MapView::elevation_map/walkability limitation). Scans every
   * cell in map bounds; for each, inverts the mouse position assuming that cell's real
   * elevation (world_to_tile()) and checks whether the result falls in that exact cell
   * — this is what makes the test diamond-exact rather than a bounding-box
   * approximation, with no separate point-in-polygon code needed. When a raised tile's
   * footprint visually overlaps a lower neighbor (both pass the test for the same
   * screen point), the candidate with the larger iso_depth_key() wins — the same
   * "larger key draws later/on top" convention the renderer already uses, so picking
   * agrees with what's visually on top.
   *
   * O(width * height) per call — a full-map scan, acceptable at this project's current
   * scale (no screen-space culling/chunking exists yet to make large maps common).
   *
   * @param mouse_x  Window-pixel mouse X (InputState::mouse_x).
   * @param mouse_y  Window-pixel mouse Y (InputState::mouse_y).
   * @param camera   Current camera (world-space top-left of the viewport).
   * @param map      Current map view.
   * @param elev_step Screen pixels lifted per unit of elevation (GameConfig::elevation_step_px).
   * @return The topmost tile under the cursor, or nullopt if none (out of bounds,
   *         empty space, or World/chunked mode).
   */
  [[nodiscard]] std::optional<TileCoord> pick_tile(float mouse_x, float mouse_y,
                                                   const corundum::gameplay::world::Camera &camera,
                                                   const corundum::gameplay::world::MapView &map,
                                                   float elev_step) noexcept;

} // namespace corundum::gameplay::sys
