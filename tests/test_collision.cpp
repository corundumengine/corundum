#include <doctest/doctest.h>

#include <corundum/core/math/vec.hpp>
#include <corundum/physics/collision.hpp>
#include <corundum/world/tilemap/tilemap.hpp>

using corundum::entities::Position;
using corundum::physics::resolve_collisions;
using corundum::physics::resolve_triangle_collisions;
using corundum::world::tilemap::CollisionRects;
using corundum::world::tilemap::CollisionRectsView;
using corundum::world::tilemap::CollisionTriangles;
using corundum::world::tilemap::TriangleCut;

namespace ccm = corundum::core::math;

// ── resolve_collisions ────────────────────────────────────────────────────────

TEST_CASE("resolve_collisions — no rects, position unchanged") {
  Position pos{100.f, 100.f};
  const Position prev{90.f, 90.f};
  resolve_collisions(pos, prev, 16.f, 16.f, CollisionRectsView{});
  CHECK(pos.col == doctest::Approx(100.f));
  CHECK(pos.row == doctest::Approx(100.f));
}

TEST_CASE("resolve_collisions — X movement into wall clamps to contact (no wall gap)") {
  // Wall at x=120, moving right from x=90 to x=110 (sprite is 16 wide). With the
  // clamp-to-contact resolver, the player ends up TOUCHING the wall (col = 120 - 16 = 104)
  // instead of being reverted to the start position — eliminates the persistent wall gap.
  CollisionRects wall;
  wall.push_back(120.f, 80.f, 32.f, 32.f);
  Position pos{110.f, 90.f};
  const Position prev{90.f, 90.f};
  resolve_collisions(pos, prev, 16.f, 16.f, wall.view());
  CHECK(pos.col == doctest::Approx(104.f));
  CHECK(pos.row == doctest::Approx(90.f));
}

TEST_CASE("resolve_collisions — X + Y block, X clamps to contact, Y passes") {
  // Wall covers (120, 80)–(152, 112). Prev (70, 70). New pos (110, 110) = 40px right + 40px down.
  CollisionRects wall;
  wall.push_back(120.f, 80.f, 32.f, 32.f);
  Position pos{110.f, 110.f};
  const Position prev{70.f, 70.f};
  resolve_collisions(pos, prev, 16.f, 16.f, wall.view());

  // X clamped to wall.col - entity_w = 120 - 16 = 104. With new col, AABB at (104, ...)
  // right edge = 120 which equals wall.left → just touching. Y then unblocked.
  CHECK(pos.col == doctest::Approx(104.f));
  CHECK(pos.row == doctest::Approx(110.f));
}

TEST_CASE("resolve_collisions — X + Y block, X clamps to contact") {
  CollisionRects wall;
  wall.push_back(120.f, 80.f, 32.f, 32.f);
  Position pos{110.f, 90.f};
  const Position prev{90.f, 90.f};
  resolve_collisions(pos, prev, 16.f, 16.f, wall.view());
  CHECK(pos.col == doctest::Approx(104.f));
  CHECK(pos.row == doctest::Approx(90.f));
}

TEST_CASE("resolve_collisions — escape rule: pre-existing overlap doesn't trap the player") {
  // Player starts INSIDE a rect (e.g. teleport put them there at spawn).
  // Trying to move should NOT freeze them — the escape rule allows motions that
  // don't increase overlap. Moving left (0.5 → 0.4) keeps the same rect overlap
  // (AABB shifts within it) so the resolver must allow it.
  CollisionRects wall;
  wall.push_back(0.f, 0.f, 1.f, 1.f); // rect [0,1] x [0,1]
  Position pos{0.4f, 0.5f};           // inside, moving left
  const Position prev{0.5f, 0.5f};
  resolve_collisions(pos, prev, 0.5f, 0.5f, wall.view());
  // Pre-existing overlap on both prev and pos — escape rule allows motion.
  CHECK(pos.col == doctest::Approx(0.4f));
  CHECK(pos.row == doctest::Approx(0.5f));
}

TEST_CASE("resolve_collisions — escape rule: motion that reduces overlap is allowed") {
  // Player at col 0.5 (overlap 0.5 wide), moves to col 1.0 (overlap 0 — touching right edge).
  // Overlap reduced, motion allowed.
  CollisionRects wall;
  wall.push_back(0.f, 0.f, 1.f, 1.f);
  Position pos{1.0f, 0.5f};
  const Position prev{0.5f, 0.5f};
  resolve_collisions(pos, prev, 0.5f, 0.5f, wall.view());
  CHECK(pos.col == doctest::Approx(1.0f));
  CHECK(pos.row == doctest::Approx(0.5f));
}

TEST_CASE("resolve_collisions — Y movement into wall from below") {
  CollisionRects wall;
  wall.push_back(100.f, 120.f, 32.f, 32.f);
  Position pos{105.f, 100.f};
  const Position prev{105.f, 70.f};
  resolve_collisions(pos, prev, 16.f, 16.f, wall.view());
  // X: right edge = 105+16 = 121 > wall.x=100, left edge 105 < wall.x+32=132 → need X check
  // X check uses (pos.x, prev.y+0) = (105, 70), AABB (105, 70, 16, 16)
  //   ax=105 < wall.x+wall.w=132 && ax1=121 > wall.x=100 → overlap X
  //   ay=70 < wall.y+wall.h=152 && ay1=86 > wall.y=120 → overlap Y
  //   → overlap → X blocked: pos.x = prev.x = 105 (still overlaps)
  // X reverted to 105 (still overlaps). Y: (105, 100, 16, 16) vs wall (100, 120, 32, 32)
  //   ax=105 < 132 && ax1=121 > 100 → overlap X
  //   ay=100 < 152 && ay1=116 > 120 → NO overlap Y
  //   → Y not blocked: pos.y stays 100
  CHECK(pos.col == doctest::Approx(105.f));
  CHECK(pos.row == doctest::Approx(100.f));
}

// ── elevation-gated collision ────────────────────────────────────────────────

TEST_CASE("resolve_collisions — elevation mismatch lets entity pass through a raised wall") {
  CollisionRects wall;
  wall.push_back(120.f, 80.f, 32.f, 32.f, 50); // wall authored at elevation 50
  Position pos{110.f, 90.f};
  const Position prev{90.f, 90.f};
  resolve_collisions(pos, prev, 16.f, 16.f, wall.view(), 0.f, /*entity_elevation=*/0, /*elevation_tolerance=*/0);
  // Entity is at elevation 0, wall at 50 — filtered out entirely, no block.
  CHECK(pos.col == doctest::Approx(110.f));
  CHECK(pos.row == doctest::Approx(90.f));
}

TEST_CASE("resolve_collisions — matching elevation still blocks movement") {
  CollisionRects wall;
  wall.push_back(120.f, 80.f, 32.f, 32.f, 50);
  Position pos{110.f, 90.f};
  const Position prev{90.f, 90.f};
  resolve_collisions(pos, prev, 16.f, 16.f, wall.view(), 0.f, /*entity_elevation=*/50, /*elevation_tolerance=*/0);
  // Clamp-to-contact: pos.col = wall.col - entity_w = 120 - 16 = 104.
  CHECK(pos.col == doctest::Approx(104.f));
  CHECK(pos.row == doctest::Approx(90.f));
}

TEST_CASE("resolve_collisions — elevation within tolerance still blocks") {
  CollisionRects wall;
  wall.push_back(120.f, 80.f, 32.f, 32.f, 50);
  Position pos{110.f, 90.f};
  const Position prev{90.f, 90.f};
  resolve_collisions(pos, prev, 16.f, 16.f, wall.view(), 0.f, /*entity_elevation=*/48, /*elevation_tolerance=*/5);
  CHECK(pos.col == doctest::Approx(104.f));
  CHECK(pos.row == doctest::Approx(90.f));
}

// ── resolve_triangle_collisions ──────────────────────────────────────────────
//
// TriangleCut names the EMPTY corner of a 1x1 tile at (0,0); a tiny 0.01x0.01
// probe box pins down a single half of the tile without axis-separated
// sliding muddying the result. k_near/k_far sit just inside the tile from a
// given corner; k_outside is far enough away on both axes that neither phase
// of resolve_triangle_collisions sees a spurious overlap.

namespace {
  constexpr float k_probe = 0.01f;
  constexpr float k_near = 0.02f;
  constexpr float k_far = 0.97f;
  constexpr Position k_outside{-1.f, -1.f};
} // namespace

TEST_CASE("resolve_triangle_collisions — NW cut: empty corner is passable") {
  CollisionTriangles tris;
  tris.push_back(0.f, 0.f, 1.f, 1.f, TriangleCut::NorthWest);
  Position pos{k_near, k_near};
  resolve_triangle_collisions(pos, k_outside, k_probe, k_probe, tris.view());
  CHECK(pos.col == doctest::Approx(k_near));
  CHECK(pos.row == doctest::Approx(k_near));
}

TEST_CASE("resolve_triangle_collisions — NW cut: solid half clamps to hypotenuse contact") {
  // Probe deep in NW cut's solid half. Clamp-to-contact places the probe exactly
  // on the empty-side boundary (u1 + v1 = 1) rather than reverting to prev row.
  // NW Y-contact: pr = ty + th * (1 - u1_norm) - h. With u1_norm = (0.97 + 0.01)/1 = 0.98:
  //   pr = 0 + 1 * (1 - 0.98) - 0.01 = 0.01.
  CollisionTriangles tris;
  tris.push_back(0.f, 0.f, 1.f, 1.f, TriangleCut::NorthWest);
  Position pos{k_far, k_far};
  resolve_triangle_collisions(pos, k_outside, k_probe, k_probe, tris.view());
  CHECK(pos.col == doctest::Approx(k_far));
  CHECK(pos.row == doctest::Approx(0.01f));
}

TEST_CASE("resolve_triangle_collisions — NE cut: empty corner is passable") {
  CollisionTriangles tris;
  tris.push_back(0.f, 0.f, 1.f, 1.f, TriangleCut::NorthEast);
  Position pos{k_far, k_near};
  resolve_triangle_collisions(pos, k_outside, k_probe, k_probe, tris.view());
  CHECK(pos.col == doctest::Approx(k_far));
  CHECK(pos.row == doctest::Approx(k_near));
}

TEST_CASE("resolve_triangle_collisions — NE cut: solid half clamps to hypotenuse contact") {
  // NE Y-contact: pr = ty + th * ((pc - tx)/tw) - h. With pc=0.02:
  //   pr = 0 + 1 * (0.02/1) - 0.01 = 0.01.
  CollisionTriangles tris;
  tris.push_back(0.f, 0.f, 1.f, 1.f, TriangleCut::NorthEast);
  Position pos{k_near, k_far};
  resolve_triangle_collisions(pos, k_outside, k_probe, k_probe, tris.view());
  CHECK(pos.col == doctest::Approx(k_near));
  CHECK(pos.row == doctest::Approx(0.01f));
}

TEST_CASE("resolve_triangle_collisions — SW cut: empty corner is passable") {
  CollisionTriangles tris;
  tris.push_back(0.f, 0.f, 1.f, 1.f, TriangleCut::SouthWest);
  Position pos{k_near, k_far};
  resolve_triangle_collisions(pos, k_outside, k_probe, k_probe, tris.view());
  CHECK(pos.col == doctest::Approx(k_near));
  CHECK(pos.row == doctest::Approx(k_far));
}

TEST_CASE("resolve_triangle_collisions — SW cut: solid half clamps to hypotenuse contact") {
  // SW Y-contact: pr = ty + th * ((pc + w - tx)/tw). With pc=0.97, w=0.01:
  //   pr = 0 + 1 * (0.98/1) = 0.98.
  CollisionTriangles tris;
  tris.push_back(0.f, 0.f, 1.f, 1.f, TriangleCut::SouthWest);
  Position pos{k_far, k_near};
  resolve_triangle_collisions(pos, k_outside, k_probe, k_probe, tris.view());
  CHECK(pos.col == doctest::Approx(k_far));
  CHECK(pos.row == doctest::Approx(0.98f));
}

TEST_CASE("resolve_triangle_collisions — SE cut: empty corner is passable") {
  CollisionTriangles tris;
  tris.push_back(0.f, 0.f, 1.f, 1.f, TriangleCut::SouthEast);
  Position pos{k_far, k_far};
  resolve_triangle_collisions(pos, k_outside, k_probe, k_probe, tris.view());
  CHECK(pos.col == doctest::Approx(k_far));
  CHECK(pos.row == doctest::Approx(k_far));
}

TEST_CASE("resolve_triangle_collisions — SE cut: solid half clamps to hypotenuse contact") {
  // SE Y-contact: pr = ty + th * (1 - (pc - tx)/tw). With pc=0.02:
  //   pr = 0 + 1 * (1 - 0.02) = 0.98.
  CollisionTriangles tris;
  tris.push_back(0.f, 0.f, 1.f, 1.f, TriangleCut::SouthEast);
  Position pos{k_near, k_near};
  resolve_triangle_collisions(pos, k_outside, k_probe, k_probe, tris.view());
  CHECK(pos.col == doctest::Approx(k_near));
  CHECK(pos.row == doctest::Approx(0.98f));
}

TEST_CASE("resolve_triangle_collisions — elevation mismatch lets entity pass through solid half") {
  CollisionTriangles tris;
  tris.push_back(0.f, 0.f, 1.f, 1.f, TriangleCut::NorthWest, 50); // triangle authored at elevation 50
  Position pos{k_far, k_far};                                     // deep in NW cut's solid half
  resolve_triangle_collisions(pos, k_outside, k_probe, k_probe, tris.view(), 0.f, /*entity_elevation=*/0,
                              /*elevation_tolerance=*/0);
  CHECK(pos.col == doctest::Approx(k_far));
  CHECK(pos.row == doctest::Approx(k_far));
}
