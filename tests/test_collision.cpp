#include <doctest/doctest.h>

#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/world/tilemap/tilemap.hpp>
#include <corundum/physics/collision.hpp>

using corundum::gameplay::ecs::Position;
using corundum::gameplay::world::tilemap::CollisionRects;
using corundum::gameplay::world::tilemap::CollisionRectsView;
using corundum::physics::sys::resolve_collisions;

namespace ccm = corundum::core::math;

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

// ── Isometric feet-offset collision ───────────────────────────────────────────

TEST_CASE("iso feet offset: anchor round-trips through cart conversion") {
  constexpr float k_half_tw = 64.f;
  constexpr float k_half_th = 32.f;
  constexpr float k_x_origin = 0.f;
  constexpr float k_bb_h = 128.f;
  constexpr float k_sprite_scale = 2.f;
  constexpr float k_feet_offset = k_bb_h * k_sprite_scale;

  const Position anchor{0.f, 10.f};
  const float feet_y = anchor.y + k_feet_offset;

  const auto cart = ccm::iso_to_cart({anchor.x, feet_y}, k_half_tw, k_half_th, k_x_origin);
  const auto iso = ccm::cart_to_iso({cart.x, cart.y}, k_half_tw, k_half_th, k_x_origin);
  const float recovered_y = iso.y - k_feet_offset;

  CHECK(iso.x == doctest::Approx(anchor.x));
  CHECK(recovered_y == doctest::Approx(anchor.y));
}

TEST_CASE("iso feet offset: round-trip with non-zero x_origin") {
  constexpr float k_half_tw = 64.f;
  constexpr float k_half_th = 32.f;
  constexpr float k_x_origin = 896.f;
  constexpr float k_bb_h = 128.f;
  constexpr float k_scale_ratio = 2.f;
  constexpr float k_feet_offset = k_bb_h * k_scale_ratio;

  const Position anchor{200.f, 400.f};
  const float feet_y = anchor.y + k_feet_offset;

  const auto cart = ccm::iso_to_cart({anchor.x, feet_y}, k_half_tw, k_half_th, k_x_origin);
  const auto iso = ccm::cart_to_iso({cart.x, cart.y}, k_half_tw, k_half_th, k_x_origin);
  const float recovered_y = iso.y - k_feet_offset;

  CHECK(iso.x == doctest::Approx(anchor.x));
  CHECK(recovered_y == doctest::Approx(anchor.y));
}

TEST_CASE("iso feet offset: no collision leaves position unchanged") {
  constexpr float k_half_tw = 64.f;
  constexpr float k_half_th = 32.f;
  constexpr float k_x_origin = 0.f;
  constexpr float k_bb_h = 128.f;
  constexpr float k_scale_ratio = 2.f;
  constexpr float k_feet_offset = k_bb_h * k_scale_ratio;

  const Position prev{0.f, 10.f};
  Position pos{-32.f, 10.f};

  const float prev_feet = prev.y + k_feet_offset;
  const float cur_feet = pos.y + k_feet_offset;

  auto prev_cart = ccm::iso_to_cart({prev.x, prev_feet}, k_half_tw, k_half_th, k_x_origin);
  auto cur_cart = ccm::iso_to_cart({pos.x, cur_feet}, k_half_tw, k_half_th, k_x_origin);
  Position pc{cur_cart.x, cur_cart.y};
  Position pcp{prev_cart.x, prev_cart.y};

  CollisionRects empty;
  resolve_collisions(pc, pcp, k_bb_h, k_bb_h, empty.view(), 0.f);

  CHECK(pc.x == doctest::Approx(cur_cart.x));
  CHECK(pc.y == doctest::Approx(cur_cart.y));
}

TEST_CASE("iso feet offset: collision resolved at feet, converted back to anchor") {
  constexpr float k_half_tw = 64.f;
  constexpr float k_half_th = 32.f;
  constexpr float k_x_origin = 0.f;
  constexpr float k_bb_h = 128.f;
  constexpr float k_scale_ratio = 2.f;
  constexpr float k_feet_offset = k_bb_h * k_scale_ratio;

  const Position prev{0.f, 10.f};
  Position pos{-32.f, 10.f};

  const float prev_feet = prev.y + k_feet_offset;
  const float cur_feet = pos.y + k_feet_offset;

  auto prev_cart = ccm::iso_to_cart({prev.x, prev_feet}, k_half_tw, k_half_th, k_x_origin);
  auto cur_cart = ccm::iso_to_cart({pos.x, cur_feet}, k_half_tw, k_half_th, k_x_origin);
  Position pc{cur_cart.x, cur_cart.y};
  Position pcp{prev_cart.x, prev_cart.y};

  CollisionRects wall;
  wall.push_back(cur_cart.x, cur_cart.y, 128.f, 128.f);
  resolve_collisions(pc, pcp, k_bb_h, k_bb_h, wall.view(), 0.f);

  CHECK(pc.x == doctest::Approx(prev_cart.x));
  CHECK(pc.y == doctest::Approx(prev_cart.y));

  const auto iso = ccm::cart_to_iso({pc.x, pc.y}, k_half_tw, k_half_th, k_x_origin);
  CHECK(iso.x == doctest::Approx(prev.x));
  CHECK(iso.y - k_feet_offset == doctest::Approx(prev.y));
}
