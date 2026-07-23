#include <doctest/doctest.h>

#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/world/tilemap/tilemap.hpp>
#include <corundum/physics/collision.hpp>

using corundum::gameplay::component::Position;
using corundum::gameplay::world::tilemap::CollisionRects;
using corundum::gameplay::world::tilemap::CollisionRectsView;
using corundum::gameplay::world::tilemap::CollisionTriangles;
using corundum::gameplay::world::tilemap::TriangleCut;
using corundum::physics::sys::resolve_collisions;
using corundum::physics::sys::resolve_triangle_collisions;

namespace ccm = corundum::core::math;

// ── resolve_collisions ────────────────────────────────────────────────────────

TEST_CASE("resolve_collisions — no rects, position unchanged") {
  Position pos{100.f, 100.f};
  const Position prev{90.f, 90.f};
  resolve_collisions(pos, prev, 16.f, 16.f, CollisionRectsView{});
  CHECK(pos.col == doctest::Approx(100.f));
  CHECK(pos.row == doctest::Approx(100.f));
}

TEST_CASE("resolve_collisions — X movement into wall is blocked") {
  // Wall at x=120, moving right from x=90 to x=110 (sprite is 16 wide, so right edge hits)
  CollisionRects wall;
  wall.push_back(120.f, 80.f, 32.f, 32.f);
  Position pos{110.f, 90.f};
  const Position prev{90.f, 90.f};
  resolve_collisions(pos, prev, 16.f, 16.f, wall.view());
  // X reverted; Y unchanged
  CHECK(pos.col == doctest::Approx(90.f));
  CHECK(pos.row == doctest::Approx(90.f));
}

TEST_CASE("resolve_collisions — X + Y block, Y block alone") {
  // Wall covers (120, 80)–(152, 112). Prev position = (70, 70).
  // New pos = (110, 110) = 40px right + 40px down.
  CollisionRects wall;
  wall.push_back(120.f, 80.f, 32.f, 32.f);
  Position pos{110.f, 110.f};
  const Position prev{70.f, 70.f};
  resolve_collisions(pos, prev, 16.f, 16.f, wall.view());

  // X blocked (right edge at 126 > wall.col=120, so X reverted to 70).
  // Y not blocked (after X reverted, right edge at 86 < wall.col=120).
  CHECK(pos.col == doctest::Approx(70.f));
  CHECK(pos.row == doctest::Approx(110.f));
}

TEST_CASE("resolve_collisions — X + Y block, X blocks first") {
  CollisionRects wall;
  wall.push_back(120.f, 80.f, 32.f, 32.f);
  Position pos{110.f, 90.f};
  const Position prev{90.f, 90.f};
  resolve_collisions(pos, prev, 16.f, 16.f, wall.view());
  // X blocked → pos.x = prev.x = 90
  // Y not blocked (with pos.x = 90, right edge = 90+16 = 106 < wall.x = 120)
  CHECK(pos.col == doctest::Approx(90.f));
  CHECK(pos.row == doctest::Approx(90.f));
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
  CHECK(pos.col == doctest::Approx(90.f));
  CHECK(pos.row == doctest::Approx(90.f));
}

TEST_CASE("resolve_collisions — elevation within tolerance still blocks") {
  CollisionRects wall;
  wall.push_back(120.f, 80.f, 32.f, 32.f, 50);
  Position pos{110.f, 90.f};
  const Position prev{90.f, 90.f};
  resolve_collisions(pos, prev, 16.f, 16.f, wall.view(), 0.f, /*entity_elevation=*/48, /*elevation_tolerance=*/5);
  CHECK(pos.col == doctest::Approx(90.f));
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
  tris.push_back(0.f, 0.f, 1.f, 1.f, TriangleCut::NW);
  Position pos{k_near, k_near};
  resolve_triangle_collisions(pos, k_outside, k_probe, k_probe, tris.view());
  CHECK(pos.col == doctest::Approx(k_near));
  CHECK(pos.row == doctest::Approx(k_near));
}

TEST_CASE("resolve_triangle_collisions — NW cut: solid half blocks movement") {
  CollisionTriangles tris;
  tris.push_back(0.f, 0.f, 1.f, 1.f, TriangleCut::NW);
  Position pos{k_far, k_far};
  resolve_triangle_collisions(pos, k_outside, k_probe, k_probe, tris.view());
  CHECK(pos.col == doctest::Approx(k_far));
  CHECK(pos.row == doctest::Approx(k_outside.row));
}

TEST_CASE("resolve_triangle_collisions — NE cut: empty corner is passable") {
  CollisionTriangles tris;
  tris.push_back(0.f, 0.f, 1.f, 1.f, TriangleCut::NE);
  Position pos{k_far, k_near};
  resolve_triangle_collisions(pos, k_outside, k_probe, k_probe, tris.view());
  CHECK(pos.col == doctest::Approx(k_far));
  CHECK(pos.row == doctest::Approx(k_near));
}

TEST_CASE("resolve_triangle_collisions — NE cut: solid half blocks movement") {
  CollisionTriangles tris;
  tris.push_back(0.f, 0.f, 1.f, 1.f, TriangleCut::NE);
  Position pos{k_near, k_far};
  resolve_triangle_collisions(pos, k_outside, k_probe, k_probe, tris.view());
  CHECK(pos.col == doctest::Approx(k_near));
  CHECK(pos.row == doctest::Approx(k_outside.row));
}

TEST_CASE("resolve_triangle_collisions — SW cut: empty corner is passable") {
  CollisionTriangles tris;
  tris.push_back(0.f, 0.f, 1.f, 1.f, TriangleCut::SW);
  Position pos{k_near, k_far};
  resolve_triangle_collisions(pos, k_outside, k_probe, k_probe, tris.view());
  CHECK(pos.col == doctest::Approx(k_near));
  CHECK(pos.row == doctest::Approx(k_far));
}

TEST_CASE("resolve_triangle_collisions — SW cut: solid half blocks movement") {
  CollisionTriangles tris;
  tris.push_back(0.f, 0.f, 1.f, 1.f, TriangleCut::SW);
  Position pos{k_far, k_near};
  resolve_triangle_collisions(pos, k_outside, k_probe, k_probe, tris.view());
  CHECK(pos.col == doctest::Approx(k_far));
  CHECK(pos.row == doctest::Approx(k_outside.row));
}

TEST_CASE("resolve_triangle_collisions — SE cut: empty corner is passable") {
  CollisionTriangles tris;
  tris.push_back(0.f, 0.f, 1.f, 1.f, TriangleCut::SE);
  Position pos{k_far, k_far};
  resolve_triangle_collisions(pos, k_outside, k_probe, k_probe, tris.view());
  CHECK(pos.col == doctest::Approx(k_far));
  CHECK(pos.row == doctest::Approx(k_far));
}

TEST_CASE("resolve_triangle_collisions — SE cut: solid half blocks movement") {
  CollisionTriangles tris;
  tris.push_back(0.f, 0.f, 1.f, 1.f, TriangleCut::SE);
  Position pos{k_near, k_near};
  resolve_triangle_collisions(pos, k_outside, k_probe, k_probe, tris.view());
  CHECK(pos.col == doctest::Approx(k_near));
  CHECK(pos.row == doctest::Approx(k_outside.row));
}

TEST_CASE("resolve_triangle_collisions — elevation mismatch lets entity pass through solid half") {
  CollisionTriangles tris;
  tris.push_back(0.f, 0.f, 1.f, 1.f, TriangleCut::NW, 50); // triangle authored at elevation 50
  Position pos{k_far, k_far};                              // deep in NW cut's solid half
  resolve_triangle_collisions(pos, k_outside, k_probe, k_probe, tris.view(), 0.f, /*entity_elevation=*/0,
                              /*elevation_tolerance=*/0);
  CHECK(pos.col == doctest::Approx(k_far));
  CHECK(pos.row == doctest::Approx(k_far));
}
