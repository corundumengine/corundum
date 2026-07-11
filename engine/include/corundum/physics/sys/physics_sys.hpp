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
  struct Scene;
} // namespace corundum::gameplay::world

namespace corundum::physics::sys {

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
                   float player_speed, corundum::core::math::IsoParams iso) noexcept;

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
                   float player_speed, corundum::core::math::IsoParams iso, float dt) noexcept;

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
   *  @post scene.pending_transition is set if a portal was entered.
   *  @performance O(n) over NPC count. No heap allocation.
   */
  void update_player(corundum::gameplay::component::TransformTable &transforms,
                     const corundum::gameplay::component::CollisionTable &collisions,
                     corundum::gameplay::entity::EntityId player, const corundum::input::InputState &input,
                     float player_speed, const corundum::gameplay::world::MapView &map,
                     corundum::gameplay::world::Scene &scene, float dt) noexcept;

} // namespace corundum::physics::sys
