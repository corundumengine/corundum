#include <corundum/physics/collision.hpp>

#include <cstdlib>
#include <utility>

namespace corundum::physics::sys {

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
        case TriangleCut::NW:
          in_empty = u1 + v1 < 1.f;
          break;
        case TriangleCut::NE:
          in_empty = u0 - v1 > 0.f;
          break;
        case TriangleCut::SW:
          in_empty = u1 - v0 < 0.f;
          break;
        case TriangleCut::SE:
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

  } // namespace

  void resolve_collisions(corundum::gameplay::component::Position &pos,
                          corundum::gameplay::component::Position prev_pos, float entity_w, float entity_h,
                          corundum::gameplay::world::tilemap::CollisionRectsView rects, float y_offset,
                          int entity_elevation, int elevation_tolerance) noexcept {
    const float eff_h = entity_h - y_offset;

    // Test X axis: use post-integrate X with pre-integrate Y.
    if (overlaps_any(pos.col, prev_pos.row + y_offset, entity_w, eff_h, rects, entity_elevation, elevation_tolerance))
      pos.col = prev_pos.col;

    // Test Y axis: use resolved X with post-integrate Y.
    if (overlaps_any(pos.col, pos.row + y_offset, entity_w, eff_h, rects, entity_elevation, elevation_tolerance))
      pos.row = prev_pos.row;
  }

  void resolve_triangle_collisions(corundum::gameplay::component::Position &pos,
                                   corundum::gameplay::component::Position prev_pos, float entity_w, float entity_h,
                                   corundum::gameplay::world::tilemap::CollisionTrianglesView triangles, float y_offset,
                                   int entity_elevation, int elevation_tolerance) noexcept {
    const float eff_h = entity_h - y_offset;

    if (overlaps_any_triangle(pos.col, prev_pos.row + y_offset, entity_w, eff_h, triangles, entity_elevation,
                              elevation_tolerance))
      pos.col = prev_pos.col;

    if (overlaps_any_triangle(pos.col, pos.row + y_offset, entity_w, eff_h, triangles, entity_elevation,
                              elevation_tolerance))
      pos.row = prev_pos.row;
  }

} // namespace corundum::physics::sys
