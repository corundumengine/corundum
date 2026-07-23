#include <doctest/doctest.h>

#include <corundum/gameplay/world/tilemap/tilemap.hpp>

namespace ctt = corundum::gameplay::world::tilemap;

namespace {

  ctt::TilemapTileset make_tileset(ctt::TileId first_gid, int tile_count, std::string material) {
    ctt::TilemapTileset ts;
    ts.first_gid = first_gid;
    ts.tile_count = tile_count;
    ts.info.tile_count = tile_count;
    ts.info.material = std::move(material);
    return ts;
  }

  ctt::Tilemap make_2x2_map(std::string material = "stone") {
    ctt::Tilemap tm;
    tm.width = 2;
    tm.height = 2;
    tm.tilesets.push_back(make_tileset(0, 4, std::move(material)));
    return tm;
  }

} // namespace

TEST_CASE("material_at — resolves the owning tileset's default material") {
  auto tm = make_2x2_map("stone");
  ctt::TilemapLayer layer;
  layer.z_index = 0;
  layer.tiles = {0, 1, 2, 3};
  tm.layers.push_back(layer);

  CHECK(ctt::material_at(tm, 0, 0) == "stone");
  CHECK(ctt::material_at(tm, 1, 1) == "stone");
}

TEST_CASE("material_at — per-cell override wins over the tileset default") {
  auto tm = make_2x2_map("stone");
  ctt::TilemapLayer layer;
  layer.z_index = 0;
  layer.tiles = {0, 1, 2, 3};
  layer.material_overrides[1] = "snow"; // cell (1,0)
  tm.layers.push_back(layer);

  CHECK(ctt::material_at(tm, 0, 0) == "stone"); // no override — tileset default
  CHECK(ctt::material_at(tm, 1, 0) == "snow");  // override wins
}

TEST_CASE("material_at — topmost z=0 layer with a tile at the cell wins") {
  auto tm = make_2x2_map("stone");

  ctt::TilemapLayer bottom;
  bottom.z_index = 0;
  bottom.tiles = {0, 0, 0, 0};
  tm.layers.push_back(bottom);

  ctt::TilemapLayer top;
  top.z_index = 0;
  top.tiles = {0, ctt::k_empty_tile, ctt::k_empty_tile, ctt::k_empty_tile};
  top.material_overrides[0] = "rug";
  tm.layers.push_back(top);

  CHECK(ctt::material_at(tm, 0, 0) == "rug");   // top layer has a tile here — wins
  CHECK(ctt::material_at(tm, 1, 0) == "stone"); // top layer empty here — falls through to bottom's default
}

TEST_CASE("material_at — animated cell resolves material via its first frame's tileset") {
  auto tm = make_2x2_map("stone");
  ctt::AnimatedCell anim;
  anim.anim_name = "flicker";
  anim.frame_gids = {1, 2};
  ctt::TilemapLayer layer;
  layer.z_index = 0;
  layer.tiles = {ctt::k_empty_tile, 0, 0, 0};
  layer.animated_cells[0] = anim;
  tm.layers.push_back(layer);

  CHECK(ctt::material_at(tm, 0, 0) == "stone");
}

TEST_CASE("material_at — no tileset default and no override returns empty string") {
  auto tm = make_2x2_map(""); // tileset has no material tag
  ctt::TilemapLayer layer;
  layer.z_index = 0;
  layer.tiles = {0, 0, 0, 0};
  tm.layers.push_back(layer);

  CHECK(ctt::material_at(tm, 0, 0) == "");
}

TEST_CASE("material_at — z_index > 0 layers are ignored") {
  auto tm = make_2x2_map("stone");
  ctt::TilemapLayer above;
  above.z_index = 1;
  above.tiles = {0, 0, 0, 0};
  above.material_overrides[0] = "lava";
  tm.layers.push_back(above);

  CHECK(ctt::material_at(tm, 0, 0) == "");
}

TEST_CASE("material_at — out-of-bounds col/row returns empty string, not UB") {
  auto tm = make_2x2_map("stone");
  ctt::TilemapLayer layer;
  layer.z_index = 0;
  layer.tiles = {0, 0, 0, 0};
  tm.layers.push_back(layer);

  CHECK(ctt::material_at(tm, -1, 0) == "");
  CHECK(ctt::material_at(tm, 0, -1) == "");
  CHECK(ctt::material_at(tm, 2, 0) == "");
  CHECK(ctt::material_at(tm, 0, 2) == "");
}

// ── in_bounds ─────────────────────────────────────────────────────────────────

TEST_CASE("in_bounds — material_at disambiguates OOB from empty default") {
  ctt::Tilemap tm;
  tm.width = 2;
  tm.height = 2;
  ctt::TilemapTileset ts;
  ts.first_gid = 0;
  ts.tile_count = 4;
  ts.info.tile_count = 4;
  ts.info.material = "";
  tm.tilesets.push_back(ts);
  ctt::TilemapLayer layer;
  layer.z_index = 0;
  layer.tiles = {0, 0, 0, 0};
  tm.layers.push_back(layer);

  CHECK(ctt::material_at(tm, 0, 0) == "");
  CHECK(ctt::material_at(tm, -1, 0) == "");
  CHECK(ctt::in_bounds(tm, 0, 0));
  CHECK_FALSE(ctt::in_bounds(tm, -1, 0));
}
