#pragma once
#include <corundum/gameplay/component/components.hpp>
#include <corundum/gameplay/world/tilemap/tilemap.hpp>

namespace corundum::physics::sys {

  /**
   * @brief Axis-separated AABB collision resolution.
   *
   * @details
   * Algorithm:
   *   1. Try X: AABB at (pos.x, prev_pos.y). If it overlaps any rect, revert pos.x.
   *   2. Try Y: AABB at (pos.x, pos.y).      If it overlaps any rect, revert pos.y.
   * This allows sliding along a wall in the non-blocked axis.
   *
   * @param pos       Post-integrate position (top-left corner, world pixels). Modified in-place.
   * @param prev_pos  Position before integrate ran this frame.
   * @param entity_w  Rendered sprite width in world pixels.
   * @param entity_h  Rendered sprite height in world pixels.
   * @param rects     World-space SoA collision rects (already scaled from Tiled pixel space).
   * @param y_offset  Shifts the top of the collision box downward, letting the upper portion
   *                  of a sprite visually overlap objects above it.
   */
  void resolve_collisions(corundum::gameplay::component::Position &pos,
                          corundum::gameplay::component::Position prev_pos, float entity_w, float entity_h,
                          corundum::gameplay::world::tilemap::CollisionRectsView rects, float y_offset = 0.f) noexcept;

  /**
   * @brief Axis-separated diagonal collision resolution for half-tile triangle shapes.
   *
   * @details Uses the same axis-separated approach as resolve_collisions, but tests
   * each AABB against a half-space defined by the triangle's hypotenuse rather than
   * a full rect. A player moving along a diagonal wall will slide naturally.
   *
   * @param pos       Post-integrate position (top-left corner, world pixels). Modified in-place.
   * @param prev_pos  Position before integrate ran this frame.
   * @param entity_w  Rendered sprite width in world pixels.
   * @param entity_h  Rendered sprite height in world pixels.
   * @param triangles World-space SoA collision triangles (already scaled from Tiled pixel space).
   * @param y_offset  Shifts the top of the collision box downward (same semantics as
   * resolve_collisions).
   */
  void resolve_triangle_collisions(corundum::gameplay::component::Position &pos,
                                   corundum::gameplay::component::Position prev_pos, float entity_w, float entity_h,
                                   corundum::gameplay::world::tilemap::CollisionTrianglesView triangles,
                                   float y_offset = 0.f) noexcept;

} // namespace corundum::physics::sys
