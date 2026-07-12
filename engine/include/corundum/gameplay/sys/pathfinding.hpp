#pragma once
#include <corundum/gameplay/sys/picking.hpp>

#include <vector>

namespace corundum::gameplay::world {
  struct MapView; // update.hpp — not included directly; see picking.hpp for why.
}

namespace corundum::gameplay::component {
  struct CollisionTable;
  struct TransformTable;
} // namespace corundum::gameplay::component

namespace corundum::gameplay::sys {

  /**
   * @brief Find a walkable path from @p start to @p goal, excluding @p start.
   *
   * @details A* over the 8-directional WalkabilityGraph (octile heuristic, cost 1.0
   * cardinal / sqrt(2) diagonal), with two additional constraints beyond plain graph
   * connectivity:
   *  - Corner-cutting is rejected: a diagonal step from a cell is only allowed if both
   *    flanking cardinal steps from that same cell are also open.
   *  - A candidate cell is rejected if any CollisionRect/CollisionTriangle whose
   *    elevation is within tolerance of that cell's own elevation overlaps it — the
   *    WalkabilityGraph alone has no notion of walls, only elevation deltas, so without
   *    this a path could route straight through a wall (the entity wouldn't clip
   *    through it, but would visibly get stuck pushing against it mid-path).
   *  - When @p npc_collisions and @p npc_transforms are both non-null, a candidate cell
   *    is also rejected if it overlaps any NPC's collision AABB. This prevents the
   *    pathfinder from routing through other entities (which would then push back
   *    reactively via physics, causing visible rubber-banding).
   *
   * Single-map mode only (returns empty if map.walkability is null, matching the
   * existing elevation_map/walkability limitation elsewhere). Returns empty if
   * unreachable, out of bounds, or start == goal.
   *
   * @param map             Current map view (walkability, elevation_map, collisions).
   * @param start           Starting cell (typically the entity's current floored position).
   * @param goal            Target cell (typically the picked/hovered tile).
   * @param npc_collisions  Entity bounding-box table for dynamic NPC obstacle avoidance; nullptr = skip.
   * @param npc_transforms  Entity position table for dynamic NPC obstacle avoidance; nullptr = skip.
   * @return Ordered waypoints from the first step after @p start through @p goal.
   */
  [[nodiscard]] std::vector<TileCoord>
  find_path(const corundum::gameplay::world::MapView &map, TileCoord start, TileCoord goal,
            const corundum::gameplay::component::CollisionTable *npc_collisions = nullptr,
            const corundum::gameplay::component::TransformTable *npc_transforms = nullptr) noexcept;

} // namespace corundum::gameplay::sys
