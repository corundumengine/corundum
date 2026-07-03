#include <doctest/doctest.h>

#include <corundum/gameplay/world/tilemap/tilemap.hpp>

namespace ctt = corundum::gameplay::world::tilemap;

namespace {

  ctt::TilemapTileset make_tileset(ctt::TileId first_gid, int tile_count) {
    ctt::TilemapTileset ts;
    ts.first_gid = first_gid;
    ts.tile_count = tile_count;
    ts.info.columns = tile_count;
    ts.info.rows = 1;
    return ts;
  }

  ctt::Tilemap make_valid_map() {
    ctt::Tilemap tm;
    tm.width = 2;
    tm.height = 2;
    tm.tilesets.push_back(make_tileset(0, 4));

    ctt::TilemapLayer layer;
    layer.name = "ground";
    layer.z_index = 0;
    layer.tiles = {0, 1, 2, 3};
    tm.layers.push_back(layer);
    return tm;
  }

} // namespace

TEST_CASE("validate — valid tilemap returns no errors") {
  const auto tm = make_valid_map();
  CHECK(ctt::validate(tm).empty());
}

TEST_CASE("validate — orphaned GID with no matching tileset") {
  auto tm = make_valid_map();
  tm.layers[0].tiles[2] = 99; // out of range for the single 4-tile tileset

  const auto errors = ctt::validate(tm);
  REQUIRE(errors.size() == 1);
  CHECK(errors[0].find("GID 99") != std::string::npos);
  CHECK(errors[0].find("col=0, row=1") != std::string::npos);
  CHECK(errors[0].find("\"ground\"") != std::string::npos);
}

TEST_CASE("validate — orphaned GID in an animated cell's frames") {
  auto tm = make_valid_map();
  ctt::AnimatedCell anim;
  anim.anim_name = "flicker";
  anim.frame_gids = {0, 1, 99};
  tm.layers[0].animated_cells[0] = anim; // cell (0,0)
  tm.layers[0].tiles[0] = ctt::k_empty_tile;

  const auto errors = ctt::validate(tm);
  REQUIRE(errors.size() == 1);
  CHECK(errors[0].find("GID 99") != std::string::npos);
  CHECK(errors[0].find("\"flicker\"") != std::string::npos);
}

TEST_CASE("validate — k_empty_tile is never flagged as orphaned") {
  auto tm = make_valid_map();
  tm.layers[0].tiles[1] = ctt::k_empty_tile;
  CHECK(ctt::validate(tm).empty());
}

TEST_CASE("validate — duplicate layer names") {
  auto tm = make_valid_map();
  ctt::TilemapLayer second;
  second.name = "ground"; // duplicate
  second.tiles = {ctt::k_empty_tile, ctt::k_empty_tile, ctt::k_empty_tile, ctt::k_empty_tile};
  tm.layers.push_back(second);

  const auto errors = ctt::validate(tm);
  REQUIRE(errors.size() == 1);
  CHECK(errors[0].find("duplicate layer name \"ground\"") != std::string::npos);
}

TEST_CASE("validate — empty layer names are not flagged as duplicates") {
  auto tm = make_valid_map();
  tm.layers[0].name.clear();
  ctt::TilemapLayer second;
  second.tiles = {ctt::k_empty_tile, ctt::k_empty_tile, ctt::k_empty_tile, ctt::k_empty_tile};
  tm.layers.push_back(second);

  CHECK(ctt::validate(tm).empty());
}

TEST_CASE("validate — collision rect extending past map bounds") {
  auto tm = make_valid_map();
  tm.collisions.push_back(1.f, 1.f, 5.f, 1.f); // col_span pushes past width=2

  const auto errors = ctt::validate(tm);
  REQUIRE(errors.size() == 1);
  CHECK(errors[0].find("collision rect") != std::string::npos);
}

TEST_CASE("validate — collision rect fully within bounds is not flagged") {
  auto tm = make_valid_map();
  tm.collisions.push_back(0.f, 0.f, 2.f, 2.f);
  CHECK(ctt::validate(tm).empty());
}

TEST_CASE("validate — negative collision rect origin is flagged") {
  auto tm = make_valid_map();
  tm.collisions.push_back(-1.f, 0.f, 1.f, 1.f);
  CHECK(ctt::validate(tm).size() == 1);
}

TEST_CASE("validate — collision triangle extending past map bounds") {
  auto tm = make_valid_map();
  tm.collision_triangles.push_back(0.f, 0.f, 1.f, 5.f, ctt::TriangleCut::NW); // row_span pushes past height=2

  const auto errors = ctt::validate(tm);
  REQUIRE(errors.size() == 1);
  CHECK(errors[0].find("collision triangle") != std::string::npos);
}
