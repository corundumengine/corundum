#include <doctest/doctest.h>

#include <corundum/gameplay/component/transform_table.hpp>
#include <corundum/gameplay/sys/picking.hpp>
#include <corundum/physics/sys/physics_sys.hpp>

using corundum::core::math::IsoParams;
using corundum::gameplay::component::TransformTable;
using corundum::gameplay::entity::EntityId;
using corundum::gameplay::sys::TileCoord;
using corundum::physics::sys::follow_path;

namespace {

  // Minimal single-entity TransformTable setup shared by these cases.
  struct Fixture {
    TransformTable transforms;
    EntityId player = static_cast<EntityId>(0);

    Fixture() {
      transforms.insert(player, 0.5f, 0.5f, 0.f, 0.f);
    }
  };

} // namespace

TEST_CASE("follow_path — aims velocity at the center of the next waypoint") {
  Fixture f;
  std::vector<TileCoord> path{{2, 0}}; // target center (2.5, 0.5) from (0.5, 0.5)
  follow_path(f.transforms, f.player, path, /*player_speed=*/1.f, IsoParams{1.f, 1.f}, /*dt=*/0.01f);

  const auto slot = f.transforms.dense_idx(f.player);
  CHECK(f.transforms.dc[slot] > 0.f); // aimed along +col
  CHECK(f.transforms.dr[slot] == doctest::Approx(0.f));
  REQUIRE(path.size() == 1); // not yet reached, waypoint still pending
}

TEST_CASE("follow_path — snaps to and pops the waypoint once reached, moving on to the next") {
  Fixture f;
  const auto slot = f.transforms.dense_idx(f.player);
  // Start right next to the first waypoint's center so a normal step reaches it.
  f.transforms.col[slot] = 1.4f;
  f.transforms.row[slot] = 0.5f;
  std::vector<TileCoord> path{{1, 0}, {2, 0}}; // first center (1.5,0.5), second (2.5,0.5)

  // player_speed*dt=0.5: covers the 0.1 remaining to the first waypoint, but not the full
  // 1.0 segment to the second one, so exactly one waypoint should be consumed.
  follow_path(f.transforms, f.player, path, /*player_speed=*/1.f, IsoParams{1.f, 1.f}, /*dt=*/0.5f);

  REQUIRE(path.size() == 1); // first waypoint popped
  CHECK(path.front().col == 2);
  // Position snapped exactly to the first waypoint's center before moving on.
  CHECK(f.transforms.col[slot] == doctest::Approx(1.5f));
  CHECK(f.transforms.row[slot] == doctest::Approx(0.5f));
  // Velocity now points at the second waypoint.
  CHECK(f.transforms.dc[slot] > 0.f);
}

TEST_CASE("follow_path — empties the path and zeroes velocity on reaching the final waypoint") {
  Fixture f;
  const auto slot = f.transforms.dense_idx(f.player);
  f.transforms.col[slot] = 1.45f;
  f.transforms.row[slot] = 0.5f;
  std::vector<TileCoord> path{{1, 0}};

  follow_path(f.transforms, f.player, path, /*player_speed=*/1.f, IsoParams{1.f, 1.f}, /*dt=*/1.f);

  CHECK(path.empty());
  CHECK(f.transforms.dc[slot] == doctest::Approx(0.f));
  CHECK(f.transforms.dr[slot] == doctest::Approx(0.f));
}

TEST_CASE("follow_path — empty path is a no-op that zeroes velocity") {
  Fixture f;
  const auto slot = f.transforms.dense_idx(f.player);
  f.transforms.dc[slot] = 5.f;
  f.transforms.dr[slot] = 5.f;
  std::vector<TileCoord> path;

  follow_path(f.transforms, f.player, path, /*player_speed=*/1.f, IsoParams{1.f, 1.f}, /*dt=*/0.1f);

  CHECK(f.transforms.dc[slot] == doctest::Approx(0.f));
  CHECK(f.transforms.dr[slot] == doctest::Approx(0.f));
}
