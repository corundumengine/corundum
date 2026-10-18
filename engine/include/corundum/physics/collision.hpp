// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/entities/components.hpp>
#include <corundum/world/tilemap/tilemap.hpp>

namespace corundum::physics {

  /**
   * @brief Elevation window a collision solve gates colliders against.
   *
   * @details Rect and triangle views carry per-collider elevations [0–255] when authored. A
   * collider is tested only while its elevation is within @ref tolerance of
   * @ref player_elevation, so a raised platform's walls block only entities standing on that
   * platform. A view with no per-collider elevation data ignores the gate entirely.
   */
  struct ElevationGate {
    int player_elevation{}; ///< The entity's own elevation [0–255].

    int tolerance{}; ///< Largest elevation difference a collider may have and still block.
  };

  /**
   * @brief Axis-separated AABB collision resolution.
   *
   * @details
   * Algorithm:
   *   1. Resolve X: AABB at (pos.col, prev_pos.row). If it introduces overlap the
   *      previous AABB did not have, clamp pos.col to the nearest contact.
   *   2. Resolve Y: AABB at (pos.col, pos.row). Same, clamping pos.row.
   * Contact-clamping (rather than reverting to prev) leaves the entity flush against
   * the geometry instead of a gap away, and resolving the axes in sequence lets it
   * slide along a wall in the non-blocked axis.
   *
   * @param pos       Post-integrate position (top-left corner). Modified in-place. In practice
   *                  callers pass tile-grid (col,row) units, matching the collision data below,
   *                  but the math itself is unit-agnostic.
   * @param prev_pos  Position before integrate ran this frame.
   * @param entity_w  Entity collision width, in the same units as pos.
   * @param entity_h  Entity collision height, in the same units as pos.
   * @param rects     SoA collision rects in tile-grid space.
   * @param y_offset  Shifts the top of the collision box downward, letting the upper portion
   *                  of a sprite visually overlap objects above it.
   * @param elevation Elevation window (see ElevationGate); ignored when @p rects carries no
   *                  per-rect elevation data.
   *
   * @pre y_offset < entity_h, so the collision box keeps a positive height.
   * @pre elevation.tolerance >= 0.
   */
  void resolve_collisions(corundum::entities::Position &pos, corundum::entities::Position prev_pos, float entity_w,
                          float entity_h, corundum::world::tilemap::CollisionRectsView rects, float y_offset = 0.f,
                          ElevationGate elevation = {}) noexcept;

  /**
   * @brief Axis-separated diagonal collision resolution for half-tile triangle shapes.
   *
   * @details Uses the same axis-separated approach as resolve_collisions, but tests
   * each AABB against a half-space defined by the triangle's hypotenuse rather than
   * a full rect. A player moving along a diagonal wall will slide naturally.
   *
   * @param pos       Post-integrate position (top-left corner). Modified in-place. In practice
   *                  callers pass tile-grid (col,row) units, matching the collision data below,
   *                  but the math itself is unit-agnostic.
   * @param prev_pos  Position before integrate ran this frame.
   * @param entity_w  Entity collision width, in the same units as pos.
   * @param entity_h  Entity collision height, in the same units as pos.
   * @param triangles SoA collision triangles in tile-grid space.
   * @param y_offset  Shifts the top of the collision box downward (same semantics as
   * resolve_collisions).
   * @param elevation Same semantics as resolve_collisions.
   *
   * @pre y_offset < entity_h, so the collision box keeps a positive height.
   * @pre elevation.tolerance >= 0.
   */
  void resolve_triangle_collisions(corundum::entities::Position &pos, corundum::entities::Position prev_pos,
                                   float entity_w, float entity_h,
                                   corundum::world::tilemap::CollisionTrianglesView triangles, float y_offset = 0.f,
                                   ElevationGate elevation = {}) noexcept;

} // namespace corundum::physics
