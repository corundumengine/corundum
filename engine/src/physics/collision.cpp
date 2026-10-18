// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/entities/components.hpp>
#include <corundum/physics/collision.hpp>
#include <corundum/world/tilemap/tilemap.hpp>

#include <cassert>
#include <cstdlib>
#include <optional>
#include <utility>

namespace corundum::physics {

  namespace {

    using corundum::world::tilemap::TriangleCut;

    // Test whether an AABB at (ax, ay, aw, ah) overlaps any rect in the SoA view.
    // The four parallel float spans let the compiler auto-vectorize this loop with SIMD.
    // Once the view carries per-rect elevations, a rect is skipped unless its elevation is
    // within elevation.tolerance of elevation.player_elevation — lets a raised platform's
    // walls block only entities standing on that platform, not something beside it.
    [[nodiscard]] bool overlaps_any(float ax, float ay, float aw, float ah,
                                    corundum::world::tilemap::CollisionRectsView rects,
                                    ElevationGate elevation) noexcept {
      const float ax1 = ax + aw;
      const float ay1 = ay + ah;
      const std::size_t n = rects.size();
      const bool filter_elevation = !rects.elevations.empty();
      for (std::size_t i = 0; i < n; ++i) {
        if (filter_elevation &&
            std::abs(static_cast<int>(rects.elevations[i]) - elevation.player_elevation) > elevation.tolerance)
          continue;
        if (ax < rects.cols[i] + rects.col_spans[i] && ax1 > rects.cols[i] && ay < rects.rows[i] + rects.row_spans[i] &&
            ay1 > rects.rows[i])
          return true;
      }
      return false;
    }

    // True when the AABB at (x, y) introduces overlap the AABB at (prev_x, prev_y) did
    // not have — the escape rule that lets an entity already inside a collider slide
    // free instead of being trapped by it.
    [[nodiscard]] bool blocks_new_overlap(float x, float y, float prev_x, float prev_y, float aw, float ah,
                                          corundum::world::tilemap::CollisionRectsView rects,
                                          ElevationGate elevation) noexcept {
      return overlaps_any(x, y, aw, ah, rects, elevation) && !overlaps_any(prev_x, prev_y, aw, ah, rects, elevation);
    }

    // Test whether an AABB at (ax, ay, aw, ah) overlaps the solid region of any triangle.
    //
    // SAT test: project the AABB onto the hypotenuse normal for each triangle.
    // If the AABB lies entirely in the empty half-space, there is no collision.
    // This correctly handles players wider or taller than the tile.
    //
    // Solid half-spaces (u=(px-tx)/tw, v=(py-ty)/th). TriangleCut names the EMPTY
    // corner (tilemap.hpp), so e.g. NW means the NW corner is empty and the rest
    // of the tile (u+v > 1) is solid:
    //   NW: solid u+v > 1  — AABB in empty if max u+v < 1  → u1+v1 < 1
    //   NE: solid u-v < 0  — AABB in empty if min u-v > 0  → u0-v1 > 0
    //   SW: solid u-v > 0  — AABB in empty if max u-v < 0  → u1-v0 < 0
    //   SE: solid u+v < 1  — AABB in empty if min u+v > 1  → u0+v0 > 1
    [[nodiscard]] bool overlaps_any_triangle(float ax, float ay, float aw, float ah,
                                             corundum::world::tilemap::CollisionTrianglesView triangles,
                                             ElevationGate elevation) noexcept {
      const float ax1 = ax + aw;
      const float ay1 = ay + ah;
      const std::size_t n = triangles.size();
      const bool filter_elevation = !triangles.elevations.empty();
      for (std::size_t i = 0; i < n; ++i) {
        if (filter_elevation &&
            std::abs(static_cast<int>(triangles.elevations[i]) - elevation.player_elevation) > elevation.tolerance)
          continue;
        const float tx = triangles.cols[i];
        const float ty = triangles.rows[i];
        const float tw = triangles.col_spans[i];
        const float th = triangles.row_spans[i];
        if (ax >= tx + tw || ax1 <= tx || ay >= ty + th || ay1 <= ty) [[likely]]
          continue;
        const float u0 = (ax - tx) / tw;  // left edge (min u)
        const float u1 = (ax1 - tx) / tw; // right edge (max u)
        const float v0 = (ay - ty) / th;  // top edge (min v)
        const float v1 = (ay1 - ty) / th; // bottom edge (max v)
        bool in_empty = false;
        switch (triangles.cuts[i]) {
          case TriangleCut::NorthWest: in_empty = u1 + v1 < 1.f; break;
          case TriangleCut::NorthEast: in_empty = u0 - v1 > 0.f; break;
          case TriangleCut::SouthWest: in_empty = u1 - v0 < 0.f; break;
          case TriangleCut::SouthEast: in_empty = u0 + v0 > 1.f; break;
          default: std::unreachable();
        }
        if (!in_empty) [[unlikely]]
          return true;
      }
      return false;
    }

    // Triangle counterpart of blocks_new_overlap.
    [[nodiscard]] bool blocks_new_overlap_triangle(float x, float y, float prev_x, float prev_y, float aw, float ah,
                                                   corundum::world::tilemap::CollisionTrianglesView triangles,
                                                   ElevationGate elevation) noexcept {
      return overlaps_any_triangle(x, y, aw, ah, triangles, elevation) &&
             !overlaps_any_triangle(prev_x, prev_y, aw, ah, triangles, elevation);
    }

    // Keep the nearest of two candidate contacts, where "nearest" is the smallest
    // candidate reachable while moving positive, and the largest while moving negative.
    // Reachable means at or beyond @p previous, so a clamp never pushes the entity
    // backwards. Returns the winning candidate, or std::nullopt if it loses.
    [[nodiscard]] std::optional<float> nearest_contact(float candidate, float previous, bool moving_positive,
                                                       float best, bool found) noexcept {
      const bool reachable = moving_positive ? candidate >= previous : candidate <= previous;
      const bool nearer = moving_positive ? candidate < best : candidate > best;
      if (reachable && (!found || nearer))
        return candidate;
      return std::nullopt;
    }

    /// X-axis contact for one triangle cut: solve its empty-side boundary for col. Each
    /// cut's boundary is one of:
    ///   NW: u1 + v1 = 1  → pc + aw = tx + tw * (1 - v1_norm)
    ///   NE: u0 - v1 = 0  → pc = tx + tw * v1_norm
    ///   SW: u1 - v0 = 0  → pc + aw = tx + tw * v0_norm
    ///   SE: u0 + v0 = 1  → pc = tx + tw * (1 - v0_norm)
    [[nodiscard]] float triangle_x_contact(float y_top, float y_bottom, float tx, float ty, float tw, float th,
                                           float aw, TriangleCut cut) noexcept {
      const float v1_norm = (y_bottom - ty) / th;
      const float v0_norm = (y_top - ty) / th;
      switch (cut) {
        case TriangleCut::NorthWest: return tx + (tw * (1.f - v1_norm)) - aw;
        case TriangleCut::NorthEast: return tx + (tw * v1_norm);
        case TriangleCut::SouthWest: return tx + (tw * v0_norm) - aw;
        case TriangleCut::SouthEast: return tx + (tw * (1.f - v0_norm));
      }
      std::unreachable();
    }

    /// Y-axis equivalent of triangle_x_contact.
    [[nodiscard]] float triangle_y_contact(float x_left, float x_right, float tx, float ty, float tw, float th,
                                           float ah, TriangleCut cut) noexcept {
      const float u1_norm = (x_right - tx) / tw;
      const float u0_norm = (x_left - tx) / tw;
      switch (cut) {
        case TriangleCut::NorthWest: return ty + (th * (1.f - u1_norm)) - ah;
        case TriangleCut::NorthEast: return ty + (th * u0_norm) - ah;
        case TriangleCut::SouthWest: return ty + (th * u1_norm);
        case TriangleCut::SouthEast: return ty + (th * (1.f - u0_norm));
      }
      std::unreachable();
    }

    // X-axis clamp-to-contact: smallest col in direction of motion such that the AABB
    // does not overlap any rect. Only valid when proposed introduces NEW overlap
    // (escape rule lets pre-existing overlap slide through). Returns prev_col unchanged
    // if no overlapping rect yields a reachable contact (shouldn't happen given the
    // caller's overlap check, but defensive).
    //
    // For "moving right" into a wall on the right: contact = rect.left - aw (player
    // just left of the wall). For "moving left": contact = rect.right. Among all
    // overlapping rects, take the smallest contact that's still >= prev_col so the
    // clamp never pushes the entity backwards.
    [[nodiscard]] float clamp_x_contact(float pos_col, float prev_col, float y_top, float aw, float ah,
                                        corundum::world::tilemap::CollisionRectsView rects,
                                        ElevationGate elevation) noexcept {
      float best = pos_col;
      bool found = false;
      const bool moving_right = pos_col > prev_col;
      const std::size_t n = rects.size();
      const bool filter_elevation = !rects.elevations.empty();
      for (std::size_t i = 0; i < n; ++i) {
        if (filter_elevation &&
            std::abs(static_cast<int>(rects.elevations[i]) - elevation.player_elevation) > elevation.tolerance)
          continue;
        if (rects.rows[i] + rects.row_spans[i] <= y_top || rects.rows[i] >= y_top + ah)
          continue;
        if (rects.cols[i] + rects.col_spans[i] <= pos_col || rects.cols[i] >= pos_col + aw)
          continue;
        const float contact = moving_right ? rects.cols[i] - aw : rects.cols[i] + rects.col_spans[i];
        if (const std::optional<float> nearest = nearest_contact(contact, prev_col, moving_right, best, found)) {
          best = *nearest;
          found = true;
        }
      }
      return found ? best : prev_col;
    }

    // Y-axis equivalent of clamp_x_contact.
    [[nodiscard]] float clamp_y_contact(float pos_row, float prev_row, float x_left, float aw, float ah,
                                        corundum::world::tilemap::CollisionRectsView rects,
                                        ElevationGate elevation) noexcept {
      float best = pos_row;
      bool found = false;
      const bool moving_down = pos_row > prev_row;
      const std::size_t n = rects.size();
      const bool filter_elevation = !rects.elevations.empty();
      for (std::size_t i = 0; i < n; ++i) {
        if (filter_elevation &&
            std::abs(static_cast<int>(rects.elevations[i]) - elevation.player_elevation) > elevation.tolerance)
          continue;
        if (rects.cols[i] + rects.col_spans[i] <= x_left || rects.cols[i] >= x_left + aw)
          continue;
        if (rects.rows[i] + rects.row_spans[i] <= pos_row || rects.rows[i] >= pos_row + ah)
          continue;
        const float contact = moving_down ? rects.rows[i] - ah : rects.rows[i] + rects.row_spans[i];
        if (const std::optional<float> nearest = nearest_contact(contact, prev_row, moving_down, best, found)) {
          best = *nearest;
          found = true;
        }
      }
      return found ? best : prev_row;
    }

    // Triangle X-axis clamp-to-contact: solve the empty-side boundary for col with the row
    // fixed (post-X-Y), then keep the closest contact reachable in the direction of motion.
    [[nodiscard]] float clamp_x_contact_triangle(float pos_col, float prev_col, float y_top, float aw, float ah,
                                                 corundum::world::tilemap::CollisionTrianglesView triangles,
                                                 ElevationGate elevation) noexcept {
      float best = pos_col;
      bool found = false;
      const bool moving_right = pos_col > prev_col;
      const std::size_t n = triangles.size();
      const bool filter_elevation = !triangles.elevations.empty();
      for (std::size_t i = 0; i < n; ++i) {
        if (filter_elevation &&
            std::abs(static_cast<int>(triangles.elevations[i]) - elevation.player_elevation) > elevation.tolerance)
          continue;
        const float tx = triangles.cols[i];
        const float ty = triangles.rows[i];
        const float tw = triangles.col_spans[i];
        const float th = triangles.row_spans[i];
        if (pos_col + aw <= tx || pos_col >= tx + tw)
          continue;
        if (y_top + ah <= ty || y_top >= ty + th)
          continue;
        const float contact = triangle_x_contact(y_top, y_top + ah, tx, ty, tw, th, aw, triangles.cuts[i]);
        if (const std::optional<float> nearest = nearest_contact(contact, prev_col, moving_right, best, found)) {
          best = *nearest;
          found = true;
        }
      }
      return found ? best : prev_col;
    }

    // Y-axis equivalent of clamp_x_contact_triangle.
    [[nodiscard]] float clamp_y_contact_triangle(float pos_row, float prev_row, float x_left, float aw, float ah,
                                                 corundum::world::tilemap::CollisionTrianglesView triangles,
                                                 ElevationGate elevation) noexcept {
      float best = pos_row;
      bool found = false;
      const bool moving_down = pos_row > prev_row;
      const std::size_t n = triangles.size();
      const bool filter_elevation = !triangles.elevations.empty();
      for (std::size_t i = 0; i < n; ++i) {
        if (filter_elevation &&
            std::abs(static_cast<int>(triangles.elevations[i]) - elevation.player_elevation) > elevation.tolerance)
          continue;
        const float tx = triangles.cols[i];
        const float ty = triangles.rows[i];
        const float tw = triangles.col_spans[i];
        const float th = triangles.row_spans[i];
        if (x_left + aw <= tx || x_left >= tx + tw)
          continue;
        if (pos_row + ah <= ty || pos_row >= ty + th)
          continue;
        const float contact = triangle_y_contact(x_left, x_left + aw, tx, ty, tw, th, ah, triangles.cuts[i]);
        if (const std::optional<float> nearest = nearest_contact(contact, prev_row, moving_down, best, found)) {
          best = *nearest;
          found = true;
        }
      }
      return found ? best : prev_row;
    }

  } // namespace

  void resolve_collisions(corundum::entities::Position &pos, corundum::entities::Position prev_pos, float entity_w,
                          float entity_h, corundum::world::tilemap::CollisionRectsView rects, float y_offset,
                          ElevationGate elevation) noexcept {
    assert(elevation.tolerance >= 0);
    const float effective_h = entity_h - y_offset;

    // Escape rule: only block moves that introduce NEW overlap (prev didn't overlap but
    // proposed does), so a teleport or geometry change that leaves the entity already
    // inside a collider doesn't trap it. X resolves first against the frame-start Y,
    // then Y against the resolved X, which lets the entity slide along a wall in the
    // non-blocked axis.
    if (pos.col != prev_pos.col && blocks_new_overlap(pos.col, prev_pos.row + y_offset, prev_pos.col,
                                                      prev_pos.row + y_offset, entity_w, effective_h, rects, elevation))
      pos.col =
          clamp_x_contact(pos.col, prev_pos.col, prev_pos.row + y_offset, entity_w, effective_h, rects, elevation);

    if (pos.row != prev_pos.row && blocks_new_overlap(pos.col, pos.row + y_offset, pos.col, prev_pos.row + y_offset,
                                                      entity_w, effective_h, rects, elevation))
      pos.row = clamp_y_contact(pos.row, prev_pos.row, pos.col, entity_w, effective_h, rects, elevation);
  }

  void resolve_triangle_collisions(corundum::entities::Position &pos, corundum::entities::Position prev_pos,
                                   float entity_w, float entity_h,
                                   corundum::world::tilemap::CollisionTrianglesView triangles, float y_offset,
                                   ElevationGate elevation) noexcept {
    assert(elevation.tolerance >= 0);
    const float effective_h = entity_h - y_offset;

    // Same escape rule and axis order as resolve_collisions; each contact is solved
    // per TriangleCut (each cut's empty boundary is a different linear equation in
    // (u, v), see clamp_x_contact_triangle's comment).
    if (pos.col != prev_pos.col &&
        blocks_new_overlap_triangle(pos.col, prev_pos.row + y_offset, prev_pos.col, prev_pos.row + y_offset, entity_w,
                                    effective_h, triangles, elevation))
      pos.col = clamp_x_contact_triangle(pos.col, prev_pos.col, prev_pos.row + y_offset, entity_w, effective_h,
                                         triangles, elevation);

    if (pos.row != prev_pos.row &&
        blocks_new_overlap_triangle(pos.col, pos.row + y_offset, pos.col, prev_pos.row + y_offset, entity_w,
                                    effective_h, triangles, elevation))
      pos.row = clamp_y_contact_triangle(pos.row, prev_pos.row, pos.col, entity_w, effective_h, triangles, elevation);
  }

} // namespace corundum::physics
