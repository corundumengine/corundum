#pragma once
#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/component/collision_table.hpp>
#include <corundum/gameplay/component/transform_table.hpp>
#include <corundum/gameplay/entity/entity.hpp>
#include <corundum/gameplay/sys/picking.hpp>
#include <corundum/input/actions.hpp>

#include <vector>

namespace corundum::gameplay::world {
  struct MapView;
  struct Portal;
  struct Scene;
} // namespace corundum::gameplay::world

namespace corundum::physics {

  /** @brief Set player velocity from held movement keys.
   *  @param[in,out] transforms  SoA table; dx/dy for @p player are modified.
   *  @param[in]     player      EntityId of the player character.
   *  @param[in]     input       Current frame input state.
   *  @param[in]     player_speed Movement speed in isometric pixels/s.
   *  @param[in]     iso         Isometric projection parameters (half_tw, half_th).
   *  @pre @p player must exist in @p transforms.
   *  @post Player dx/dy set to 0 then adjusted for held directions; speed is normalised.
   *  @performance O(1).
   */
  void apply_input(corundum::gameplay::component::TransformTable &transforms,
                   corundum::gameplay::entity::EntityId player, const corundum::input::InputState &input,
                   float player_speed, corundum::core::math::IsometricParams iso) noexcept;

  /** @brief Advance @p e's position by velocity * dt.
   *  @param[in,out] transforms  SoA table; col/row for @p e are advanced.
   *  @param[in]     e           Entity to integrate.
   *  @param[in]     dt          Fixed timestep in seconds.
   *  @pre @p e must exist in @p transforms.
   *  @post Entity position updated by velocity * dt.
   */
  void integrate(corundum::gameplay::component::TransformTable &transforms, corundum::gameplay::entity::EntityId e,
                 float dt) noexcept;

  /** @brief Drive velocity toward the next waypoint in a click-to-move path.
   *
   * Aims at the center of path.front() (col+0.5, row+0.5). Pops the waypoint and moves
   * on to the next one once this frame's movement would reach or pass it (uses dt to
   * detect this robustly rather than an arbitrary distance epsilon). Zeroes velocity
   * once the path empties.
   *
   *  @param[in,out] transforms  SoA table; dc/dr for @p player are modified.
   *  @param[in]     player      EntityId of the player character.
   *  @param[in,out] path        Remaining waypoints; front is popped on arrival.
   *  @param[in]     player_speed Movement speed in isometric pixels/s.
   *  @param[in]     iso         Isometric projection parameters (half_tw, half_th).
   *  @param[in]     dt          Fixed timestep in seconds.
   *  @pre @p player must exist in @p transforms.
   */
  void follow_path(corundum::gameplay::component::TransformTable &transforms,
                   corundum::gameplay::entity::EntityId player, std::vector<corundum::gameplay::sys::TileCoord> &path,
                   float player_speed, corundum::core::math::IsometricParams iso, float dt) noexcept;

  /** @brief Full player step: input → integrate → collision resolve → portal detect.
   *
   * Applies input, integrates velocity, converts iso↔Cartesian for collision
   * resolution against tile rects and triangles, resolves NPC body collisions,
   * clamps to map bounds, and sets scene.pending_transition on portal overlap.
   *
   *  @param[in,out] transforms    SoA table; read/written for player movement.
   *  @param[in]     collisions    Collision bounding boxes for all entities.
   *  @param[in]     player        EntityId of the player character.
   *  @param[in]     input         Current frame input state.
   *  @param[in]     player_speed  Movement speed in px/s.
   *  @param[in]     map           Active map view (tile colliders, bounds, portals).
   *  @param[in,out] scene         May receive a pending_transition on portal overlap.
   *  @param[in]     dt            Fixed timestep in seconds.
   *  @pre @p player must exist in @p transforms and @p collisions.
   *  @post Player position is clamped to map bounds.
   *  @post For cross-map and return-to-world portals, scene.transition_prompt is set and
   *        scene.mode becomes Prompt; handle_map_transition() runs the transition only after
   *        the player confirms. Chunk-to-chunk portals teleport the player immediately.
   *  @performance O(n) over NPC count. No heap allocation.
   */
  void update_player(corundum::gameplay::component::TransformTable &transforms,
                     const corundum::gameplay::component::CollisionTable &collisions,
                     corundum::gameplay::entity::EntityId player, const corundum::input::InputState &input,
                     float player_speed, const corundum::gameplay::world::MapView &map,
                     corundum::gameplay::world::Scene &scene, float dt) noexcept;

  /**
   * @brief Result of compute_elevation_gate: the integer elevation to gate
   * colliders against, plus a ramp-aware tolerance that widens while the player
   * stands on a ramp cell so both ends' colliders apply mid-ramp.
   */
  struct ElevationGate {
    int player_elevation; ///< Rounded interpolated elevation at (col, row).
    int tolerance;        ///< Ramp Δ/2 (ceil) while on a ramp; 0 elsewhere.
  };

  /**
   * @brief Compute the elevation gate used to filter authored colliders by elevation.
   *
   * Rounds the interpolated elevation (was: truncates — see plan §3c) so a player at
   * elev 3.99 on a 0→4 ramp gates at elev 4 instead of elev 3 (where authored top-ramp
   * colliders were invisible). While the feet cell is a ramp, widens the tolerance to
   * ceil(Δ/2) so both end elevations stay within range — without widening, the
   * midpoint player (rounded to 2) would filter out colliders at both 0 and 4.
   *
   * @param[in] map   Active map view (uses elevation_map if set; World mode → defaults).
   * @param[in] col   Fractional tile column (player's pre-integrate position).
   * @param[in] row   Fractional tile row.
   * @return Gate value (player elevation + tolerance) to pass into resolve_collisions /
   *         resolve_triangle_collisions.
   */
  [[nodiscard]] ElevationGate compute_elevation_gate(const corundum::gameplay::world::MapView &map, float col,
                                                     float row) noexcept;

  /**
   * @brief True if the player is at an elevation that matches the portal's cell elevation.
   *
   * Looks up the portal's center cell elevation (rounded) and compares against
   * @p player_elev within @p elev_tolerance. Without this gate, a player standing on
   * a bridge (elev 5) would trigger a ground-floor portal (elev 0) authored at the
   * same cell — they teleport down every time they cross it. Plan §4a.
   *
   * @param map             Active map view (uses elevation_map; World mode falls back to elev 0).
   * @param portal          Portal whose center cell is the gating reference.
   * @param player_elev     Player's rounded gate value (from compute_elevation_gate).
   * @param elev_tolerance  Ramp-aware tolerance; 0 for off-ramp gating.
   */
  [[nodiscard]] bool portal_elev_matches(const corundum::gameplay::world::MapView &map,
                                         const corundum::gameplay::world::Portal &portal, int player_elev,
                                         int elev_tolerance) noexcept;

} // namespace corundum::physics
