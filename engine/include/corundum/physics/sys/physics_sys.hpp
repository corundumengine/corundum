#pragma once
#include <corundum/gameplay/component/collision_table.hpp>
#include <corundum/gameplay/component/transform_table.hpp>
#include <corundum/gameplay/entity/entity.hpp>
#include <corundum/input/actions.hpp>

namespace corundum::gameplay::world {
  struct MapView;
  struct Scene;
} // namespace corundum::gameplay::world

namespace corundum::physics::sys {

  /** @brief Set player velocity from held movement keys.
   *  @param[in,out] transforms  SoA table; dx/dy for @p player are modified.
   *  @param[in]     player      EntityId of the player character.
   *  @param[in]     input       Current frame input state.
   *  @param[in]     speed       Movement speed in px/s.
   *  @pre @p player must exist in @p transforms.
   *  @post Player dx/dy set to 0 then adjusted for held directions; speed is normalised.
   *  @performance O(1).
   */
  void apply_input(corundum::gameplay::component::TransformTable &transforms,
                   corundum::gameplay::entity::EntityId player, const corundum::input::InputState &input,
                   float speed) noexcept;

  /** @brief Advance positions by velocity * dt for all active transforms.
   *  @param[in,out] transforms  SoA table; x/y are advanced by dx/dy * dt.
   *  @param[in]     dt          Fixed timestep in seconds.
   *  @post All entity positions updated by velocity * dt.
   *  @performance O(n) over active entity count. Auto-vectorisable SoA layout.
   */
  void integrate(corundum::gameplay::component::TransformTable &transforms, float dt) noexcept;

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
