#pragma once
#include <corundum/core/game_config.hpp>
#include <corundum/gameplay/dialogue/registry.hpp>
#include <corundum/gameplay/world/portals/portal.hpp>
#include <corundum/gameplay/world/scene.hpp>
#include <corundum/gameplay/world/tilemap/tilemap.hpp>
#include <corundum/gameplay/world/tilemap/walkability.hpp>
#include <corundum/input/actions.hpp>

#include <span>

namespace corundum::render::data {
  struct RenderState;
}

namespace corundum::gameplay::quest {
  class Registry;
}

namespace corundum::gameplay::world {

  /**
   * @brief Non-owning view over the map data the update loop needs.
   *
   * Carries pre-built, world-space collision data and total world dimensions
   * so the update loop can work for both single-tilemap and multi-chunk modes.
   */
  struct MapView {
    corundum::gameplay::world::tilemap::CollisionRectsView collisions;
    corundum::gameplay::world::tilemap::CollisionTrianglesView collision_triangles;
    float world_w_px = 0.f;      ///< Total isometric world width in display pixels.
    float world_h_px = 0.f;      ///< Total isometric world height in display pixels.
    float world_w_tiles = 0.f;   ///< Total map width in tile-grid columns.
    float world_h_tiles = 0.f;   ///< Total map height in tile-grid rows.
    float half_tw = 0.f;         ///< Half the scaled diamond width; used for iso↔cart conversion.
    float half_th = 0.f;         ///< Half the scaled diamond height; used for iso↔cart conversion.
    float x_origin = 0.f;        ///< Isometric x-shift so the leftmost tile lands at x = 0.
    float character_scale = 1.f; ///< Character/entity sprite render scale.
    float tile_scale = 1.f;      ///< Tile render scale.
    std::span<const corundum::gameplay::world::Portal> portals;
    /// Single-map tilemap, used to look up an entity's own elevation for elevation-aware
    /// collision. Null in chunked/streamed World mode (use world_render + elevation_at_tile instead).
    const corundum::gameplay::world::tilemap::Tilemap *elevation_map = nullptr;
    /// Walkability graph for movement gating across too-steep elevation edges. Null in
    /// chunked/streamed World mode — same limitation as elevation_map.
    const corundum::gameplay::world::tilemap::WalkabilityGraph *walkability = nullptr;
    /// Active-chunk window for world-mode elevation lookups via elevation_under().
    /// Set by build_map_view() only in World render mode; nullptr in single-map mode.
    const corundum::render::data::RenderState *world_render = nullptr;
  };

  /** @brief Elevation of the tile under (col_f, row_f) for any render mode.
   *
   * Routes through elevation_map (single-map, interpolated) or world_render
   * (world-mode, discrete chunk lookup via render::sys::elevation_under).
   * Returns 0 if no elevation data is available at the queried position.
   *
   * @param[in] map   MapView built for the current frame.
   * @param[in] col_f Fractional tile column.
   * @param[in] row_f Fractional tile row.
   * @return Tile elevation (≥0) at the queried position; 0 when out of bounds.
   */
  [[nodiscard]] float elevation_at_tile(const MapView &map, float col_f, float row_f) noexcept;

  /**
   * @brief Advance game state by one fixed timestep.
   *
   * Drives dialogue or exploring physics depending on the current mode.
   * Writes a pending_transition into @p scene when the player steps on a portal.
   *
   * @param scene       All mutable game-world state.
   * @param cfg         Immutable game configuration.
   * @param graphs      Loaded dialogue graphs.
   * @param input       Current frame input state.
   * @param map         Non-owning view of the current map's tilemap and portals.
   * @param dt          Fixed timestep in seconds.
   * @param win_w       Live window width in screen pixels.
   * @param win_h       Live window height in screen pixels.
   * @param flags       Persistent game flags (quest progress, dialogue visit counts).
   */
  void update(corundum::gameplay::world::Scene &scene, const corundum::core::GameConfig &cfg,
              const corundum::gameplay::dialogue::Registry &graphs, const corundum::input::InputState &input,
              const MapView &map, float dt, float win_w, float win_h, corundum::gameplay::FlagStore &flags,
              const quest::Registry *quests = nullptr);

} // namespace corundum::gameplay::world
