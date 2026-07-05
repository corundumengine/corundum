#pragma once
#include <corundum/gameplay/component/components.hpp>
#include <corundum/gameplay/world/tilemap/walkability.hpp>

namespace corundum::physics::sys {

  /**
   * @brief Gate movement against a WalkabilityGraph, on top of (not instead of)
   * ordinary collision resolution.
   *
   * @details Deliberately separate from resolve_collisions/resolve_triangle_collisions
   * — collision gates against explicit authored geometry, this gates against terrain
   * elevation deltas the walkability graph encodes. Axis-separated like
   * resolve_collisions: reverts pos.col if the column crossed into a disconnected
   * cell, then the same for pos.row. Operates on the entity's actual feet position,
   * not a collision AABB.
   *
   * @param pos       Post-integrate (and post-collision-resolution) position. Modified in-place.
   * @param prev_pos  Position before integrate ran this frame.
   * @param graph     Walkability graph to test against, or nullptr to disable (no-op) —
   *                  e.g. chunked/streamed World mode, where this isn't wired up yet.
   */
  void resolve_walkability(corundum::gameplay::component::Position &pos,
                           corundum::gameplay::component::Position prev_pos,
                           const corundum::gameplay::world::tilemap::WalkabilityGraph *graph) noexcept;

} // namespace corundum::physics::sys
