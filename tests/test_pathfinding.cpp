#include <doctest/doctest.h>

#include <corundum/gameplay/sys/pathfinding.hpp>
#include <corundum/gameplay/world/tilemap/tilemap.hpp>
#include <corundum/gameplay/world/update.hpp>

using corundum::gameplay::sys::find_path;
using corundum::gameplay::world::MapView;
using corundum::gameplay::world::tilemap::CollisionRects;
using corundum::gameplay::world::tilemap::Tilemap;
using corundum::gameplay::world::tilemap::TilemapLayer;

namespace {

  constexpr int k_max_step_height = 4;

  Tilemap make_map(int width, int height) {
    Tilemap tm;
    tm.width = width;
    tm.height = height;
    TilemapLayer layer;
    layer.name = "ground";
    layer.z_index = 0;
    layer.visible = true;
    layer.tiles.assign(static_cast<std::size_t>(width * height), 1);
    layer.elevation.assign(static_cast<std::size_t>(width * height), 0);
    tm.layers.push_back(std::move(layer));
    return tm;
  }

  void set_elevation(Tilemap &tm, int col, int row, uint8_t elev) {
    tm.layers[0].elevation[static_cast<std::size_t>(row * tm.width + col)] = elev;
  }

} // namespace

TEST_CASE("find_path — straight diagonal path on a flat open map") {
  Tilemap tm = make_map(5, 5);
  const auto graph = corundum::gameplay::world::tilemap::build_walkability_graph(tm, k_max_step_height);

  MapView map;
  map.elevation_map = &tm;
  map.walkability = &graph;

  const auto path = find_path(map, {0, 0}, {4, 4});
  REQUIRE(path.size() == 4);
  CHECK(path.back().col == 4);
  CHECK(path.back().row == 4);
  // Every step should be diagonal (both col and row advance) — the shortest route
  // on a fully open flat map.
  int pc = 0, pr = 0;
  for (const auto &step : path) {
    CHECK(step.col == pc + 1);
    CHECK(step.row == pr + 1);
    pc = step.col;
    pr = step.row;
  }
}

TEST_CASE("find_path — goal isolated by a steep elevation wall is unreachable") {
  Tilemap tm = make_map(3, 1);
  set_elevation(tm, 1, 0, 50); // the only cell between start and goal, walled off both ways
  const auto graph = corundum::gameplay::world::tilemap::build_walkability_graph(tm, k_max_step_height);

  MapView map;
  map.elevation_map = &tm;
  map.walkability = &graph;

  const auto path = find_path(map, {0, 0}, {2, 0});
  CHECK(path.empty());
}

TEST_CASE("find_path — corner-cutting through a blocked pair of neighbors is rejected") {
  // (0,0) -> (1,1) diagonal edge is elevation-open (both at 0), but both flanking
  // cardinal neighbors (1,0) and (0,1) are walled off from (0,0) by elevation — the
  // corner-cut rule must reject the diagonal shortcut, leaving (1,1) unreachable.
  Tilemap tm = make_map(2, 2);
  set_elevation(tm, 1, 0, 50);
  set_elevation(tm, 0, 1, 50);
  const auto graph = corundum::gameplay::world::tilemap::build_walkability_graph(tm, k_max_step_height);

  MapView map;
  map.elevation_map = &tm;
  map.walkability = &graph;

  const auto path = find_path(map, {0, 0}, {1, 1});
  CHECK(path.empty());
}

TEST_CASE("find_path — routes around a collision rect rather than through it") {
  Tilemap tm = make_map(5, 5);
  const auto graph = corundum::gameplay::world::tilemap::build_walkability_graph(tm, k_max_step_height);

  CollisionRects wall;
  // Column 2 blocked except row 0 — leaves exactly one gap to route through.
  for (int row = 1; row < 5; ++row)
    wall.push_back(2.f, static_cast<float>(row), 1.f, 1.f);

  MapView map;
  map.elevation_map = &tm;
  map.walkability = &graph;
  map.collisions = wall.view();

  const auto path = find_path(map, {0, 2}, {4, 2});
  REQUIRE_FALSE(path.empty());
  CHECK(path.back().col == 4);
  CHECK(path.back().row == 2);
  bool crossed_via_gap = false;
  for (const auto &step : path) {
    if (step.col == 2) {
      REQUIRE(step.row == 0); // the only open cell in column 2
      crossed_via_gap = true;
    }
  }
  CHECK(crossed_via_gap);
}

TEST_CASE("find_path — a collision shape at a different elevation doesn't block a ground-level path") {
  Tilemap tm = make_map(3, 1);
  const auto graph = corundum::gameplay::world::tilemap::build_walkability_graph(tm, k_max_step_height);

  CollisionRects wall;
  wall.push_back(1.f, 0.f, 1.f, 1.f, 50); // authored at elevation 50; path cells are all at 0

  MapView map;
  map.elevation_map = &tm;
  map.walkability = &graph;
  map.collisions = wall.view();

  const auto path = find_path(map, {0, 0}, {2, 0});
  REQUIRE(path.size() == 2);
  CHECK(path[0].col == 1);
  CHECK(path[1].col == 2);
}

TEST_CASE("find_path — start equals goal returns empty") {
  Tilemap tm = make_map(3, 3);
  const auto graph = corundum::gameplay::world::tilemap::build_walkability_graph(tm, k_max_step_height);
  MapView map;
  map.elevation_map = &tm;
  map.walkability = &graph;
  CHECK(find_path(map, {1, 1}, {1, 1}).empty());
}

TEST_CASE("find_path — null walkability (World mode) returns empty") {
  Tilemap tm = make_map(3, 3);
  MapView map;
  map.elevation_map = &tm;
  map.walkability = nullptr;
  CHECK(find_path(map, {0, 0}, {2, 2}).empty());
}
