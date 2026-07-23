#include <doctest/doctest.h>

#include <corundum/gameplay/world/tilemap/tilemap.hpp>

namespace ctt = corundum::gameplay::world::tilemap;

namespace {

  ctt::Tilemap make_2x2_map() {
    ctt::Tilemap tm;
    tm.width = 2;
    tm.height = 2;
    return tm;
  }

} // namespace

TEST_CASE("elevation_at — single z=0 layer with elevation data") {
  auto tm = make_2x2_map();
  ctt::TilemapLayer layer;
  layer.z_index = 0;
  layer.tiles = {0, 0, 0, 0};
  layer.elevation = {5, 10, 0, 3};
  tm.layers.push_back(layer);

  CHECK(ctt::elevation_at(tm, 0, 0) == 5);
  CHECK(ctt::elevation_at(tm, 1, 0) == 10);
  CHECK(ctt::elevation_at(tm, 0, 1) == 0);
  CHECK(ctt::elevation_at(tm, 1, 1) == 3);
}

TEST_CASE("elevation_at — topmost z=0 layer with a tile at the cell wins") {
  auto tm = make_2x2_map();

  ctt::TilemapLayer bottom;
  bottom.z_index = 0;
  bottom.tiles = {0, 0, 0, 0};
  bottom.elevation = {1, 1, 1, 1};
  tm.layers.push_back(bottom);

  // Top layer only has a tile at cell (0,0); elsewhere it's empty, so bottom's elevation shows through.
  ctt::TilemapLayer top;
  top.z_index = 0;
  top.tiles = {0, ctt::k_empty_tile, ctt::k_empty_tile, ctt::k_empty_tile};
  top.elevation = {9, 9, 9, 9};
  tm.layers.push_back(top);

  CHECK(ctt::elevation_at(tm, 0, 0) == 9); // top layer has a tile here — wins
  CHECK(ctt::elevation_at(tm, 1, 0) == 1); // top layer empty here — falls through to bottom
}

TEST_CASE("elevation_at — empty elevation vector defaults to 0") {
  auto tm = make_2x2_map();
  ctt::TilemapLayer layer;
  layer.z_index = 0;
  layer.tiles = {0, 0, 0, 0};
  // layer.elevation left empty.
  tm.layers.push_back(layer);

  CHECK(ctt::elevation_at(tm, 0, 0) == 0);
}

TEST_CASE("elevation_at — z_index > 0 layers are ignored") {
  auto tm = make_2x2_map();
  ctt::TilemapLayer above;
  above.z_index = 1;
  above.tiles = {0, 0, 0, 0};
  above.elevation = {42, 42, 42, 42};
  tm.layers.push_back(above);

  CHECK(ctt::elevation_at(tm, 0, 0) == 0);
}

TEST_CASE("elevation_at — out-of-bounds col/row returns 0, not UB") {
  auto tm = make_2x2_map();
  ctt::TilemapLayer layer;
  layer.z_index = 0;
  layer.tiles = {0, 0, 0, 0};
  layer.elevation = {5, 5, 5, 5};
  tm.layers.push_back(layer);

  CHECK(ctt::elevation_at(tm, -1, 0) == 0);
  CHECK(ctt::elevation_at(tm, 0, -1) == 0);
  CHECK(ctt::elevation_at(tm, 2, 0) == 0);
  CHECK(ctt::elevation_at(tm, 0, 2) == 0);
}

// ── ramp_axis_at / interpolated_elevation_at ─────────────────────────────────

TEST_CASE("ramp_axis_at — resolves a marked cell, nullopt elsewhere") {
  auto tm = make_2x2_map();
  ctt::TilemapLayer layer;
  layer.z_index = 0;
  layer.tiles = {0, 0, 0, 0};
  layer.ramps[0] = ctt::RampAxis::NORTH_SOUTH; // cell (0,0)
  tm.layers.push_back(layer);

  REQUIRE(ctt::ramp_axis_at(tm, 0, 0).has_value());
  CHECK(*ctt::ramp_axis_at(tm, 0, 0) == ctt::RampAxis::NORTH_SOUTH);
  CHECK_FALSE(ctt::ramp_axis_at(tm, 1, 0).has_value());
  CHECK_FALSE(ctt::ramp_axis_at(tm, -1, 0).has_value());
}

TEST_CASE("interpolated_elevation_at — non-ramp cell falls back to the discrete elevation") {
  auto tm = make_2x2_map();
  ctt::TilemapLayer layer;
  layer.z_index = 0;
  layer.tiles = {0, 0, 0, 0};
  layer.elevation = {5, 10, 0, 3};
  tm.layers.push_back(layer);

  CHECK(ctt::interpolated_elevation_at(tm, 0.5f, 0.5f) == doctest::Approx(5.f));
  CHECK(ctt::interpolated_elevation_at(tm, 1.5f, 0.5f) == doctest::Approx(10.f));
}

TEST_CASE("interpolated_elevation_at — a north-south ramp blends between its neighbors") {
  // 1x3 column: (0,0) high, (0,1) is the ramp, (0,2) low.
  ctt::Tilemap tm;
  tm.width = 1;
  tm.height = 3;
  ctt::TilemapLayer layer;
  layer.z_index = 0;
  layer.tiles = {0, 0, 0};
  layer.elevation = {10, 5, 0};
  layer.ramps[1] = ctt::RampAxis::NORTH_SOUTH; // cell (0,1)
  tm.layers.push_back(layer);

  // At the north edge of the ramp cell (row_f == 1.0): matches the north neighbor's elevation.
  CHECK(ctt::interpolated_elevation_at(tm, 0.5f, 1.0f) == doctest::Approx(10.f));
  // A quarter of the way across: 3/4 of the way from the north neighbor to the south neighbor.
  CHECK(ctt::interpolated_elevation_at(tm, 0.5f, 1.25f) == doctest::Approx(7.5f));
  // Midway across the ramp cell: halfway between the two neighbors.
  CHECK(ctt::interpolated_elevation_at(tm, 0.5f, 1.5f) == doctest::Approx(5.f));
  // Just short of the south edge: nearly the south neighbor's elevation.
  CHECK(ctt::interpolated_elevation_at(tm, 0.5f, 1.9f) == doctest::Approx(1.f));
}

// ── in_bounds ─────────────────────────────────────────────────────────────────

TEST_CASE("in_bounds — returns true for valid indices") {
  ctt::Tilemap tm;
  tm.width = 3;
  tm.height = 4;
  CHECK(ctt::in_bounds(tm, 0, 0));
  CHECK(ctt::in_bounds(tm, 2, 3));
  CHECK(ctt::in_bounds(tm, 1, 2));
}

TEST_CASE("in_bounds — returns false for negative and overflow indices") {
  ctt::Tilemap tm;
  tm.width = 3;
  tm.height = 4;
  CHECK_FALSE(ctt::in_bounds(tm, -1, 0));
  CHECK_FALSE(ctt::in_bounds(tm, 0, -1));
  CHECK_FALSE(ctt::in_bounds(tm, 3, 0));
  CHECK_FALSE(ctt::in_bounds(tm, 0, 4));
}

TEST_CASE("in_bounds — disambiguates OOB sentinel from legitimate zero elevation") {
  ctt::Tilemap tm;
  tm.width = 2;
  tm.height = 2;
  ctt::TilemapLayer layer;
  layer.z_index = 0;
  layer.tiles = {0, 0, 0, 0};
  layer.elevation = {0, 0, 0, 0};
  tm.layers.push_back(layer);

  CHECK(ctt::elevation_at(tm, 0, 0) == 0);
  CHECK(ctt::elevation_at(tm, -1, 0) == 0);
  CHECK(ctt::in_bounds(tm, 0, 0));
  CHECK_FALSE(ctt::in_bounds(tm, -1, 0));
}
