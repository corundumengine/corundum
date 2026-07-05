#include <doctest/doctest.h>

#include <corundum/gameplay/world/tilemap/tilemap.hpp>
#include <corundum/gameplay/world/tilemap/walkability.hpp>
#include <corundum/physics/walkability.hpp>

using corundum::gameplay::component::Position;
using corundum::gameplay::world::tilemap::build_walkability_graph;
using corundum::gameplay::world::tilemap::Tilemap;
using corundum::gameplay::world::tilemap::TilemapLayer;
using corundum::gameplay::world::tilemap::WalkabilityGraph;
using corundum::physics::sys::resolve_walkability;

namespace {

  // A 3x3 flat map (elevation 0 everywhere) with one raised tile at the caller-chosen
  // (raised_col, raised_row) with elevation raised_elev.
  Tilemap make_test_map(int raised_col, int raised_row, uint8_t raised_elev) {
    Tilemap tm;
    tm.width = 3;
    tm.height = 3;

    TilemapLayer layer;
    layer.name = "ground";
    layer.z_index = 0;
    layer.visible = true;
    layer.tiles.assign(9, 1); // non-empty tile everywhere so elevation_at() sees a tile present
    layer.elevation.assign(9, 0);
    layer.elevation[static_cast<std::size_t>(raised_row * tm.width + raised_col)] = raised_elev;
    tm.layers.push_back(std::move(layer));
    return tm;
  }

} // namespace

// ── build_walkability_graph ──────────────────────────────────────────────────

TEST_CASE("build_walkability_graph — disconnects an edge exceeding max_step_height") {
  const Tilemap tm = make_test_map(1, 1, 50);
  const WalkabilityGraph g = build_walkability_graph(tm, /*max_step_height=*/4);
  // (1,1) is elevated 50; its flat neighbor (0,1) should be disconnected both ways.
  CHECK_FALSE(g.can_move(1, 1, 0, 1));
  CHECK_FALSE(g.can_move(0, 1, 1, 1));
}

TEST_CASE("build_walkability_graph — keeps an edge within max_step_height connected") {
  const Tilemap tm = make_test_map(1, 1, 3);
  const WalkabilityGraph g = build_walkability_graph(tm, /*max_step_height=*/4);
  CHECK(g.can_move(1, 1, 0, 1));
  CHECK(g.can_move(0, 1, 1, 1));
}

TEST_CASE("WalkabilityGraph::can_move — same cell, non-adjacent, and out-of-bounds are unblocked") {
  const Tilemap tm = make_test_map(1, 1, 50);
  const WalkabilityGraph g = build_walkability_graph(tm, /*max_step_height=*/4);
  CHECK(g.can_move(0, 0, 0, 0));   // same cell
  CHECK(g.can_move(0, 0, 2, 2));   // not grid-adjacent
  CHECK(g.can_move(-1, 0, 0, 0));  // source out of bounds
  CHECK(g.can_move(0, 0, -1, -1)); // target out of bounds
}

TEST_CASE("WalkabilityGraph::set_passable — toggles symmetrically") {
  const Tilemap tm = make_test_map(1, 1, 50);
  WalkabilityGraph g = build_walkability_graph(tm, /*max_step_height=*/4);
  REQUIRE_FALSE(g.can_move(1, 1, 0, 1));
  g.set_passable(1, 1, 0, 1, true);
  CHECK(g.can_move(1, 1, 0, 1));
  CHECK(g.can_move(0, 1, 1, 1));
  g.set_passable(1, 1, 0, 1, false);
  CHECK_FALSE(g.can_move(1, 1, 0, 1));
  CHECK_FALSE(g.can_move(0, 1, 1, 1));
}

// ── resolve_walkability ──────────────────────────────────────────────────────

TEST_CASE("resolve_walkability — null graph is a no-op") {
  Position pos{1.5f, 1.5f};
  const Position prev{0.5f, 1.5f};
  resolve_walkability(pos, prev, nullptr);
  CHECK(pos.col == doctest::Approx(1.5f));
  CHECK(pos.row == doctest::Approx(1.5f));
}

TEST_CASE("resolve_walkability — reverts the column crossing into a disconnected cell") {
  const Tilemap tm = make_test_map(1, 1, 50);
  const WalkabilityGraph g = build_walkability_graph(tm, /*max_step_height=*/4);
  Position pos{1.5f, 0.5f}; // moving into cell (1,0)... col crosses from cell 0 to cell 1 at row 0
  const Position prev{0.5f, 0.5f};
  resolve_walkability(pos, prev, &g);
  // (1,0) and (0,0) are both flat (raised tile is at (1,1)) — this move should be unaffected.
  CHECK(pos.col == doctest::Approx(1.5f));

  // Now move from (0,1) into the raised cell (1,1): should be blocked.
  Position pos2{1.5f, 1.5f};
  const Position prev2{0.5f, 1.5f};
  resolve_walkability(pos2, prev2, &g);
  CHECK(pos2.col == doctest::Approx(prev2.col));
}

TEST_CASE("resolve_walkability — leaves position alone when crossing a connected edge") {
  const Tilemap tm = make_test_map(1, 1, 3);
  const WalkabilityGraph g = build_walkability_graph(tm, /*max_step_height=*/4);
  Position pos{1.5f, 1.5f};
  const Position prev{0.5f, 1.5f};
  resolve_walkability(pos, prev, &g);
  CHECK(pos.col == doctest::Approx(1.5f));
  CHECK(pos.row == doctest::Approx(1.5f));
}
