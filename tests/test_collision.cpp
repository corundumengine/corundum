#include <doctest/doctest.h>

#include <corundum/gameplay/world/tilemap/tilemap.hpp>
#include <corundum/physics/collision.hpp>

using corundum::gameplay::ecs::Position;
using corundum::gameplay::world::tilemap::CollisionRects;
using corundum::gameplay::world::tilemap::CollisionRectsView;
using corundum::physics::sys::resolve_collisions;

// ── resolve_collisions ────────────────────────────────────────────────────────

TEST_CASE("resolve_collisions — no rects, position unchanged") {
  Position pos{100.f, 100.f};
  const Position prev{90.f, 90.f};
  resolve_collisions(pos, prev, 16.f, 16.f, CollisionRectsView{});
  CHECK(pos.x == doctest::Approx(100.f));
  CHECK(pos.y == doctest::Approx(100.f));
}

TEST_CASE("resolve_collisions — X movement into wall is blocked") {
  // Wall at x=120, moving right from x=90 to x=110 (sprite is 16 wide, so right edge hits)
  CollisionRects wall;
  wall.push_back(120.f, 80.f, 32.f, 32.f);
  Position pos{110.f, 90.f};
  const Position prev{90.f, 90.f};
  resolve_collisions(pos, prev, 16.f, 16.f, wall.view());
  // X reverted; Y unchanged
  CHECK(pos.x == doctest::Approx(90.f));
  CHECK(pos.y == doctest::Approx(90.f));
}

TEST_CASE("resolve_collisions — Y movement into floor is blocked") {
  // Floor at y=120, moving down from y=90 to y=110 (sprite is 16 tall, so bottom edge hits)
  CollisionRects floor;
  floor.push_back(80.f, 120.f, 32.f, 32.f);
  Position pos{90.f, 110.f};
  const Position prev{90.f, 90.f};
  resolve_collisions(pos, prev, 16.f, 16.f, floor.view());
  // Y reverted; X unchanged
  CHECK(pos.x == doctest::Approx(90.f));
  CHECK(pos.y == doctest::Approx(90.f));
}

TEST_CASE("resolve_collisions — diagonal into wall allows slide on clear axis") {
  // Wall to the right; player moving right and down. X is blocked, Y should be free.
  CollisionRects wall;
  wall.push_back(120.f, 80.f, 32.f, 32.f);
  // Moving from (90,90) to (110,110)
  Position pos{110.f, 110.f};
  const Position prev{90.f, 90.f};
  resolve_collisions(pos, prev, 16.f, 16.f, wall.view());
  // X is blocked by wall (x=110 + w=16 = 126 > wall.x=120)
  CHECK(pos.x == doctest::Approx(90.f));
  // Y at (90, 110) with w=16, h=16: right=106, bottom=126 vs wall x=120..152, y=80..112
  // AABB: [90,106] x [110,126] vs wall [120,152] x [80,112]: 106 < 120, no overlap → Y free
  CHECK(pos.y == doctest::Approx(110.f));
}

TEST_CASE("resolve_collisions — entity already inside rect at prev_pos is not ejected") {
  // prev_pos is already inside the rect; only the current-frame motion is reverted
  CollisionRects block;
  block.push_back(50.f, 50.f, 100.f, 100.f);
  // Both prev and new are inside the block
  Position pos{80.f, 80.f};
  const Position prev{70.f, 70.f};
  resolve_collisions(pos, prev, 16.f, 16.f, block.view());
  // Both axes blocked → both reverted to prev values (no ejection beyond that)
  CHECK(pos.x == doctest::Approx(70.f));
  CHECK(pos.y == doctest::Approx(70.f));
}
