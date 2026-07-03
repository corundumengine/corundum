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
