// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/entities/entity.hpp>
#include <corundum/entities/tables/collision_table.hpp>
#include <corundum/entities/tables/transform_table.hpp>
#include <corundum/world/map_view.hpp>
#include <corundum/world/tilemap/walkability.hpp>
#include <cstddef>
#include <cstdint>
#include <doctest/doctest.h>

#include <algorithm>
#include <corundum/world/pathfinding.hpp>
#include <corundum/world/tilemap/tilemap.hpp>
#include <utility>

using corundum::world::find_path;
using corundum::world::MapView;
using corundum::world::tilemap::CollisionRects;
using corundum::world::tilemap::Tilemap;
using corundum::world::tilemap::TilemapLayer;

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
  const auto graph = corundum::world::tilemap::build_walkability_graph(tm, k_max_step_height);

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
  const auto graph = corundum::world::tilemap::build_walkability_graph(tm, k_max_step_height);

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
  const auto graph = corundum::world::tilemap::build_walkability_graph(tm, k_max_step_height);

  MapView map;
  map.elevation_map = &tm;
  map.walkability = &graph;

  const auto path = find_path(map, {0, 0}, {1, 1});
  CHECK(path.empty());
}

TEST_CASE("find_path — routes around a collision rect rather than through it") {
  Tilemap tm = make_map(5, 5);
  const auto graph = corundum::world::tilemap::build_walkability_graph(tm, k_max_step_height);

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
  const auto graph = corundum::world::tilemap::build_walkability_graph(tm, k_max_step_height);

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
  const auto graph = corundum::world::tilemap::build_walkability_graph(tm, k_max_step_height);
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

TEST_CASE("find_path — routes around an NPC's footprint, not through the cell it blocks") {
  // An entity's footprint extends up from its feet (collision_table.hpp), so an NPC at
  // (2,3) with a 1x1 box covers rows 2..3 — including the cell *north* of its own tile.
  // Testing [row, row + row_span] instead left that cell looking free, so the path was
  // routed straight through it and the player jammed against the NPC.
  Tilemap tm = make_map(5, 5);
  const auto graph = corundum::world::tilemap::build_walkability_graph(tm, k_max_step_height);

  corundum::entities::EntityManager entities;
  corundum::entities::CollisionTable npc_collisions;
  corundum::entities::TransformTable npc_transforms;
  const corundum::entities::EntityId npc = entities.create();
  npc_transforms.insert(npc, 2.f, 3.f, 0.f, 0.f);
  npc_collisions.insert(npc, 1.f, 1.f);

  MapView map;
  map.elevation_map = &tm;
  map.walkability = &graph;

  const corundum::world::TileCoord start{.col = 0, .row = 2};
  const corundum::world::TileCoord goal{.col = 4, .row = 2};
  const auto path = find_path(map, start, goal, &npc_collisions, &npc_transforms);
  REQUIRE_FALSE(path.empty());
  CHECK(path.back() == goal);

  // The detour goes around the footprint's cells rather than straight through them.
  const auto on_npc = [](const corundum::world::TileCoord &step) noexcept {
    return step == corundum::world::TileCoord{.col = 1, .row = 2} ||
           step == corundum::world::TileCoord{.col = 2, .row = 2};
  };
  CHECK_FALSE(std::ranges::any_of(path, on_npc));
}

TEST_CASE("find_path — the mover's own footprint is not treated as an obstacle") {
  // A mover's footprint straddles the cells around its feet, so leaving it in the NPC scan
  // marks its own first step blocked. Callers pass the mover as `exclude` to avoid that.
  Tilemap tm = make_map(3, 1);
  const auto graph = corundum::world::tilemap::build_walkability_graph(tm, k_max_step_height);

  corundum::entities::EntityManager entities;
  corundum::entities::CollisionTable collisions;
  corundum::entities::TransformTable transforms;
  const corundum::entities::EntityId mover = entities.create();
  transforms.insert(mover, 1.f, 0.5f, 0.f, 0.f);
  collisions.insert(mover, 0.5f, 0.5f);

  MapView map;
  map.elevation_map = &tm;
  map.walkability = &graph;

  // Footprint is cols 0.75..1.25 — it covers cell (1,0), the only step east.
  const corundum::world::TileCoord start{.col = 0, .row = 0};
  const corundum::world::TileCoord goal{.col = 2, .row = 0};
  CHECK(find_path(map, start, goal, &collisions, &transforms).empty());

  const auto path = find_path(map, start, goal, &collisions, &transforms, mover);
  REQUIRE_FALSE(path.empty());
  CHECK(path.back() == goal);
}
