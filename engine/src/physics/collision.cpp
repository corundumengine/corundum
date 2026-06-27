#include <corundum/physics/collision.hpp>

#include <utility>

namespace corundum::physics::sys {

  namespace {

    // Test whether an AABB at (ax, ay, aw, ah) overlaps any rect in the SoA view.
    // The four parallel float spans let the compiler auto-vectorize this loop with SIMD.
    [[nodiscard]] bool overlaps_any(float ax, float ay, float aw, float ah,
                                    corundum::gameplay::world::tilemap::CollisionRectsView rects) noexcept {
      const float ax1 = ax + aw;
      const float ay1 = ay + ah;
      const std::size_t n = rects.size();
      for (std::size_t i = 0; i < n; ++i) {
        if (ax < rects.xs[i] + rects.ws[i] && ax1 > rects.xs[i] && ay < rects.ys[i] + rects.hs[i] && ay1 > rects.ys[i])
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
    // Solid half-spaces (u=(px-tx)/tw, v=(py-ty)/th):
    //   NW: solid u+v < 1  — AABB in empty if min u+v > 1  → u0+v0 > 1
    //   NE: solid u-v > 0  — AABB in empty if max u-v < 0  → u1-v0 < 0
    //   SW: solid u-v < 0  — AABB in empty if min u-v > 0  → u0-v1 > 0
    //   SE: solid u+v > 1  — AABB in empty if max u+v < 1  → u1+v1 < 1
    [[nodiscard]] bool overlaps_any_triangle(float ax, float ay, float aw, float ah,
                                             corundum::gameplay::world::tilemap::CollisionTrianglesView tris) noexcept {
      using corundum::gameplay::world::tilemap::TriangleCut;
      const float ax1 = ax + aw;
      const float ay1 = ay + ah;
      const std::size_t n = tris.size();
      for (std::size_t i = 0; i < n; ++i) {
        const float tx = tris.xs[i], ty = tris.ys[i];
        const float tw = tris.ws[i], th = tris.hs[i];
        if (ax >= tx + tw || ax1 <= tx || ay >= ty + th || ay1 <= ty) [[likely]]
          continue;
        const float u0 = (ax - tx) / tw;  // left edge (min u)
        const float u1 = (ax1 - tx) / tw; // right edge (max u)
        const float v0 = (ay - ty) / th;  // top edge (min v)
        const float v1 = (ay1 - ty) / th; // bottom edge (max v)
        bool in_empty = false;
        switch (tris.cuts[i]) {
        case TriangleCut::NW:
          in_empty = u0 + v0 > 1.f;
          break;
        case TriangleCut::NE:
          in_empty = u1 - v0 < 0.f;
          break;
        case TriangleCut::SW:
          in_empty = u0 - v1 > 0.f;
          break;
        case TriangleCut::SE:
          in_empty = u1 + v1 < 1.f;
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
                          corundum::gameplay::world::tilemap::CollisionRectsView rects, float y_offset) noexcept {
    const float eff_h = entity_h - y_offset;

    // Test X axis: use post-integrate X with pre-integrate Y.
    if (overlaps_any(pos.x, prev_pos.y + y_offset, entity_w, eff_h, rects))
      pos.x = prev_pos.x;

    // Test Y axis: use resolved X with post-integrate Y.
    if (overlaps_any(pos.x, pos.y + y_offset, entity_w, eff_h, rects))
      pos.y = prev_pos.y;
  }

  void resolve_triangle_collisions(corundum::gameplay::component::Position &pos,
                                   corundum::gameplay::component::Position prev_pos, float entity_w, float entity_h,
                                   corundum::gameplay::world::tilemap::CollisionTrianglesView triangles,
                                   float y_offset) noexcept {
    const float eff_h = entity_h - y_offset;

    if (overlaps_any_triangle(pos.x, prev_pos.y + y_offset, entity_w, eff_h, triangles))
      pos.x = prev_pos.x;

    if (overlaps_any_triangle(pos.x, pos.y + y_offset, entity_w, eff_h, triangles))
      pos.y = prev_pos.y;
  }

} // namespace corundum::physics::sys
