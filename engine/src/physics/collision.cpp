#include <corundum/physics/collision.hpp>

#include <cstdlib>
#include <utility>

namespace corundum::physics {

  namespace {

    // Test whether an AABB at (ax, ay, aw, ah) overlaps any rect in the SoA view.
    // The four parallel float spans let the compiler auto-vectorize this loop with SIMD.
    // If rects.elevations is non-empty, a rect is skipped unless its elevation is within
    // elevation_tolerance of entity_elevation — lets a raised platform's walls block only
    // entities standing on that platform, not something walking underneath/beside it.
    [[nodiscard]] bool overlaps_any(float ax, float ay, float aw, float ah,
                                    corundum::gameplay::world::tilemap::CollisionRectsView rects, int entity_elevation,
                                    int elevation_tolerance) noexcept {
      const float ax1 = ax + aw;
      const float ay1 = ay + ah;
      const std::size_t n = rects.size();
      const bool filter_elevation = !rects.elevations.empty();
      for (std::size_t i = 0; i < n; ++i) {
        if (filter_elevation &&
            std::abs(static_cast<int>(rects.elevations[i]) - entity_elevation) > elevation_tolerance)
          continue;
        if (ax < rects.cols[i] + rects.col_spans[i] && ax1 > rects.cols[i] && ay < rects.rows[i] + rects.row_spans[i] &&
            ay1 > rects.rows[i])
          return true;
      }
      return false;
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
                                             corundum::gameplay::world::tilemap::CollisionTrianglesView tris,
                                             int entity_elevation, int elevation_tolerance) noexcept {
      using corundum::gameplay::world::tilemap::TriangleCut;
      const float ax1 = ax + aw;
      const float ay1 = ay + ah;
      const std::size_t n = tris.size();
      const bool filter_elevation = !tris.elevations.empty();
      for (std::size_t i = 0; i < n; ++i) {
        if (filter_elevation && std::abs(static_cast<int>(tris.elevations[i]) - entity_elevation) > elevation_tolerance)
          continue;
        const float tx = tris.cols[i], ty = tris.rows[i];
        const float tw = tris.col_spans[i], th = tris.row_spans[i];
        if (ax >= tx + tw || ax1 <= tx || ay >= ty + th || ay1 <= ty) [[likely]]
          continue;
        const float u0 = (ax - tx) / tw;  // left edge (min u)
        const float u1 = (ax1 - tx) / tw; // right edge (max u)
        const float v0 = (ay - ty) / th;  // top edge (min v)
        const float v1 = (ay1 - ty) / th; // bottom edge (max v)
        bool in_empty = false;
        switch (tris.cuts[i]) {
          case TriangleCut::NorthWest:
            in_empty = u1 + v1 < 1.f;
            break;
          case TriangleCut::NorthEast:
            in_empty = u0 - v1 > 0.f;
            break;
          case TriangleCut::SouthWest:
            in_empty = u1 - v0 < 0.f;
            break;
          case TriangleCut::SouthEast:
            in_empty = u0 + v0 > 1.f;
            break;
          default:
            std::unreachable();
        }
        if (!in_empty) [[unlikely]]
          return true;
      }
      return false;
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
                                        corundum::gameplay::world::tilemap::CollisionRectsView rects,
                                        int entity_elevation, int elevation_tolerance) noexcept {
      float best = pos_col;
      bool found = false;
      const bool moving_right = pos_col > prev_col;
      const std::size_t n = rects.size();
      const bool filter_elev = !rects.elevations.empty();
      for (std::size_t i = 0; i < n; ++i) {
        if (filter_elev && std::abs(static_cast<int>(rects.elevations[i]) - entity_elevation) > elevation_tolerance)
          continue;
        if (rects.rows[i] + rects.row_spans[i] <= y_top || rects.rows[i] >= y_top + ah)
          continue;
        if (rects.cols[i] + rects.col_spans[i] <= pos_col || rects.cols[i] >= pos_col + aw)
          continue;
        const float contact = moving_right ? rects.cols[i] - aw : rects.cols[i] + rects.col_spans[i];
        if (moving_right) {
          if (contact >= prev_col && (!found || contact < best)) {
            best = contact;
            found = true;
          }
        } else {
          if (contact <= prev_col && (!found || contact > best)) {
            best = contact;
            found = true;
          }
        }
      }
      return found ? best : prev_col;
    }

    // Y-axis equivalent of clamp_x_contact.
    [[nodiscard]] float clamp_y_contact(float pos_row, float prev_row, float x_left, float aw, float ah,
                                        corundum::gameplay::world::tilemap::CollisionRectsView rects,
                                        int entity_elevation, int elevation_tolerance) noexcept {
      float best = pos_row;
      bool found = false;
      const bool moving_down = pos_row > prev_row;
      const std::size_t n = rects.size();
      const bool filter_elev = !rects.elevations.empty();
      for (std::size_t i = 0; i < n; ++i) {
        if (filter_elev && std::abs(static_cast<int>(rects.elevations[i]) - entity_elevation) > elevation_tolerance)
          continue;
        if (rects.cols[i] + rects.col_spans[i] <= x_left || rects.cols[i] >= x_left + aw)
          continue;
        if (rects.rows[i] + rects.row_spans[i] <= pos_row || rects.rows[i] >= pos_row + ah)
          continue;
        const float contact = moving_down ? rects.rows[i] - ah : rects.rows[i] + rects.row_spans[i];
        if (moving_down) {
          if (contact >= prev_row && (!found || contact < best)) {
            best = contact;
            found = true;
          }
        } else {
          if (contact <= prev_row && (!found || contact > best)) {
            best = contact;
            found = true;
          }
        }
      }
      return found ? best : prev_row;
    }

    // Triangle X-axis clamp-to-contact: solve the empty-side boundary equation for col,
    // given the row is fixed (post-X-Y). Each TriangleCut's boundary is one of:
    //   NW: u1 + v1 = 1  → pc + aw = tx + tw * (1 - v1_norm)
    //   NE: u0 - v1 = 0  → pc = tx + tw * v1_norm
    //   SW: u1 - v0 = 0  → pc + aw = tx + tw * v0_norm
    //   SE: u0 + v0 = 1  → pc = tx + tw * (1 - v0_norm)
    // Same min/max-over-contacts filter as rects (closest reachable contact in direction
    // of motion).
    [[nodiscard]] float clamp_x_contact_triangle(float pos_col, float prev_col, float y_top, float aw, float ah,
                                                 corundum::gameplay::world::tilemap::CollisionTrianglesView tris,
                                                 int entity_elevation, int elevation_tolerance) noexcept {
      using corundum::gameplay::world::tilemap::TriangleCut;
      float best = pos_col;
      bool found = false;
      const bool moving_right = pos_col > prev_col;
      const std::size_t n = tris.size();
      const bool filter_elev = !tris.elevations.empty();
      for (std::size_t i = 0; i < n; ++i) {
        if (filter_elev && std::abs(static_cast<int>(tris.elevations[i]) - entity_elevation) > elevation_tolerance)
          continue;
        const float tx = tris.cols[i], ty = tris.rows[i];
        const float tw = tris.col_spans[i], th = tris.row_spans[i];
        if (pos_col + aw <= tx || pos_col >= tx + tw)
          continue;
        if (y_top + ah <= ty || y_top >= ty + th)
          continue;
        const float v1_norm = (y_top + ah - ty) / th;
        const float v0_norm = (y_top - ty) / th;
        const float u1_target = 1.f - v1_norm;
        float contact;
        switch (tris.cuts[i]) {
          case TriangleCut::NorthWest:
            // pc + aw = tx + tw * u1_target
            contact = tx + tw * u1_target - aw;
            break;
          case TriangleCut::NorthEast:
            // pc = tx + tw * v1_norm
            contact = tx + tw * v1_norm;
            break;
          case TriangleCut::SouthWest:
            // pc + aw = tx + tw * v0_norm
            contact = tx + tw * v0_norm - aw;
            break;
          case TriangleCut::SouthEast:
            // pc = tx + tw * (1 - v0_norm)
            contact = tx + tw * (1.f - v0_norm);
            break;
          default:
            std::unreachable();
        }
        if (moving_right) {
          if (contact >= prev_col && (!found || contact < best)) {
            best = contact;
            found = true;
          }
        } else {
          if (contact <= prev_col && (!found || contact > best)) {
            best = contact;
            found = true;
          }
        }
      }
      return found ? best : prev_col;
    }

    // Y-axis equivalent of clamp_x_contact_triangle.
    [[nodiscard]] float clamp_y_contact_triangle(float pos_row, float prev_row, float x_left, float aw, float ah,
                                                 corundum::gameplay::world::tilemap::CollisionTrianglesView tris,
                                                 int entity_elevation, int elevation_tolerance) noexcept {
      using corundum::gameplay::world::tilemap::TriangleCut;
      float best = pos_row;
      bool found = false;
      const bool moving_down = pos_row > prev_row;
      const std::size_t n = tris.size();
      const bool filter_elev = !tris.elevations.empty();
      for (std::size_t i = 0; i < n; ++i) {
        if (filter_elev && std::abs(static_cast<int>(tris.elevations[i]) - entity_elevation) > elevation_tolerance)
          continue;
        const float tx = tris.cols[i], ty = tris.rows[i];
        const float tw = tris.col_spans[i], th = tris.row_spans[i];
        if (x_left + aw <= tx || x_left >= tx + tw)
          continue;
        if (pos_row + ah <= ty || pos_row >= ty + th)
          continue;
        const float u1_norm = (x_left + aw - tx) / tw;
        const float u0_norm = (x_left - tx) / tw;
        float contact;
        switch (tris.cuts[i]) {
          case TriangleCut::NorthWest:
            // pr + ah = ty + th * (1 - u1_norm)
            contact = ty + th * (1.f - u1_norm) - ah;
            break;
          case TriangleCut::NorthEast:
            // pr + ah = ty + th * u0_norm
            contact = ty + th * u0_norm - ah;
            break;
          case TriangleCut::SouthWest:
            // pr = ty + th * u1_norm
            contact = ty + th * u1_norm;
            break;
          case TriangleCut::SouthEast:
            // pr = ty + th * (1 - u0_norm)
            contact = ty + th * (1.f - u0_norm);
            break;
          default:
            std::unreachable();
        }
        if (moving_down) {
          if (contact >= prev_row && (!found || contact < best)) {
            best = contact;
            found = true;
          }
        } else {
          if (contact <= prev_row && (!found || contact > best)) {
            best = contact;
            found = true;
          }
        }
      }
      return found ? best : prev_row;
    }

  } // namespace

  void resolve_collisions(corundum::gameplay::component::Position &pos,
                          corundum::gameplay::component::Position prev_pos, float entity_w, float entity_h,
                          corundum::gameplay::world::tilemap::CollisionRectsView rects, float y_offset,
                          int entity_elevation, int elevation_tolerance) noexcept {
    const float eff_h = entity_h - y_offset;

    // Escape rule: only block moves that introduce NEW overlap (prev didn't overlap
    // this rect but proposed does). Pre-existing overlap lets the entity slide freely
    // — important when teleports or geometry changes leave the entity already inside
    // a collider. See plan §2b.

    // X-axis: clamp-to-contact on new overlap, using prev-row (frame-start Y).
    if (pos.col != prev_pos.col) {
      const bool prev_overlaps = overlaps_any(prev_pos.col, prev_pos.row + y_offset, entity_w, eff_h, rects,
                                              entity_elevation, elevation_tolerance);
      const bool new_overlaps =
          overlaps_any(pos.col, prev_pos.row + y_offset, entity_w, eff_h, rects, entity_elevation, elevation_tolerance);
      if (new_overlaps && !prev_overlaps)
        pos.col = clamp_x_contact(pos.col, prev_pos.col, prev_pos.row + y_offset, entity_w, eff_h, rects,
                                  entity_elevation, elevation_tolerance);
    }

    // Y-axis: same, using the resolved col (post-X) for the X range.
    if (pos.row != prev_pos.row) {
      const bool prev_overlaps =
          overlaps_any(pos.col, prev_pos.row + y_offset, entity_w, eff_h, rects, entity_elevation, elevation_tolerance);
      const bool new_overlaps =
          overlaps_any(pos.col, pos.row + y_offset, entity_w, eff_h, rects, entity_elevation, elevation_tolerance);
      if (new_overlaps && !prev_overlaps)
        pos.row = clamp_y_contact(pos.row, prev_pos.row, pos.col, entity_w, eff_h, rects, entity_elevation,
                                  elevation_tolerance);
    }
  }

  void resolve_triangle_collisions(corundum::gameplay::component::Position &pos,
                                   corundum::gameplay::component::Position prev_pos, float entity_w, float entity_h,
                                   corundum::gameplay::world::tilemap::CollisionTrianglesView triangles, float y_offset,
                                   int entity_elevation, int elevation_tolerance) noexcept {
    const float eff_h = entity_h - y_offset;

    // Same escape-rule / clamp-to-contact logic as resolve_collisions; contact is
    // solved per-TriangleCut (each cut's empty boundary is a different linear equation
    // in (u, v), see clamp_x_contact_triangle's comment).

    if (pos.col != prev_pos.col) {
      const bool prev_overlaps = overlaps_any_triangle(prev_pos.col, prev_pos.row + y_offset, entity_w, eff_h,
                                                       triangles, entity_elevation, elevation_tolerance);
      const bool new_overlaps = overlaps_any_triangle(pos.col, prev_pos.row + y_offset, entity_w, eff_h, triangles,
                                                      entity_elevation, elevation_tolerance);
      if (new_overlaps && !prev_overlaps)
        pos.col = clamp_x_contact_triangle(pos.col, prev_pos.col, prev_pos.row + y_offset, entity_w, eff_h, triangles,
                                           entity_elevation, elevation_tolerance);
    }

    if (pos.row != prev_pos.row) {
      const bool prev_overlaps = overlaps_any_triangle(pos.col, prev_pos.row + y_offset, entity_w, eff_h, triangles,
                                                       entity_elevation, elevation_tolerance);
      const bool new_overlaps = overlaps_any_triangle(pos.col, pos.row + y_offset, entity_w, eff_h, triangles,
                                                      entity_elevation, elevation_tolerance);
      if (new_overlaps && !prev_overlaps)
        pos.row = clamp_y_contact_triangle(pos.row, prev_pos.row, pos.col, entity_w, eff_h, triangles, entity_elevation,
                                           elevation_tolerance);
    }
  }

} // namespace corundum::physics
