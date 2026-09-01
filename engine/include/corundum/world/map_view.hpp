#pragma once
#include <corundum/core/game_config.hpp>
#include <corundum/world/portals/portal.hpp>
#include <corundum/world/tilemap/tilemap.hpp>
#include <corundum/world/tilemap/walkability.hpp>

#include <span>

namespace corundum::render {
  struct RenderState;
}

namespace corundum::world {

  /**
   * @brief Non-owning view over the map data the update loop needs.
   *
   * Carries pre-built, world-space collision data and total world dimensions
   * so the update loop can work for both single-tilemap and multi-chunk modes.
   */
  struct MapView {
    corundum::world::tilemap::CollisionRectsView collisions;
    corundum::world::tilemap::CollisionTrianglesView collision_triangles;
    float world_w_px = 0.f;      ///< Total isometric world width in display pixels.
    float world_h_px = 0.f;      ///< Total isometric world height in display pixels.
    float world_w_tiles = 0.f;   ///< Total map width in tile-grid columns.
    float world_h_tiles = 0.f;   ///< Total map height in tile-grid rows.
    float half_tw = 0.f;         ///< Half the scaled diamond width; used for iso↔cart conversion.
    float half_th = 0.f;         ///< Half the scaled diamond height; used for iso↔cart conversion.
    float x_origin = 0.f;        ///< Isometric x-shift so the leftmost tile lands at x = 0.
    float character_scale = 1.f; ///< Character/entity sprite render scale.
    float tile_scale = 1.f;      ///< Tile render scale.
    std::span<const corundum::world::Portal> portals;
    /// Single-map tilemap, used to look up an entity's own elevation for elevation-aware
    /// collision. Null in chunked/streamed World mode (use world_render + elevation_at_tile instead).
    const corundum::world::tilemap::Tilemap *elevation_map = nullptr;
    /// Walkability graph for movement gating across too-steep elevation edges. Null in
    /// chunked/streamed World mode — same limitation as elevation_map.
    const corundum::world::tilemap::WalkabilityGraph *walkability = nullptr;
    /// Active-chunk window for world-mode elevation lookups via elevation_under().
    /// Set by build_map_view() only in World render mode; nullptr in single-map mode.
    const corundum::render::RenderState *world_render = nullptr;
  };

  /** @brief Elevation of the tile under (col_f, row_f) for any render mode.
   *
   * Routes through elevation_map (single-map, interpolated) or world_render
   * (world-mode, discrete chunk lookup via render::elevation_under).
   * Returns 0 if no elevation data is available at the queried position.
   *
   * @param[in] map   MapView built for the current frame.
   * @param[in] col_f Fractional tile column.
   * @param[in] row_f Fractional tile row.
   * @return Tile elevation (≥0) at the queried position; 0 when out of bounds.
   */
  [[nodiscard]] float elevation_at_tile(const MapView &map, float col_f, float row_f) noexcept;

  /**
   * @brief Build a MapView from render state for the current frame's simulation step.
   *
   * Reads tile dimensions, isometric math, and collision views from the render
   * pipeline and packages them into the non-owning MapView consumed by update().
   * Handles both single-map and multi-chunk render modes.
   *
   * @param[in] render Current render state (collision data, tilemap info).
   * @param[in] cfg    Game configuration (tile scale).
   * @return A fully-populated MapView ready for the simulation step.
   */
  [[nodiscard]] MapView build_map_view(render::RenderState &render, const core::GameConfig &cfg) noexcept;

} // namespace corundum::world
