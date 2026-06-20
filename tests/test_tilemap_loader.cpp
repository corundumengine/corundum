#include <doctest/doctest.h>

#include <corundum/gameplay/world/tilemap/loader.hpp>
#include <corundum/gameplay/world/tilemap/tilemap.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

// ── Temp-file helpers ─────────────────────────────────────────────────────────

namespace {

  void write_file(const fs::path &p, std::string_view content) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p);
    f << content;
  }

  fs::path temp_dir(std::string_view tag) {
    const auto p = fs::temp_directory_path() / "crpg_test_tilemap" / tag;
    fs::create_directories(p);
    return p;
  }

  constexpr std::string_view TILESET_A_JSON =
      R"({"path":"game/assets/textures/tileset.png","tile_width":16,"tile_height":16,"columns":8,"rows":4})";

  constexpr std::string_view TILESET_B_JSON =
      R"({"path":"game/assets/textures/tileset2.png","tile_width":16,"tile_height":16,"columns":4,"rows":4})";

  // Builds a minimal single-tileset 2×1 map using tileset A (32 tiles, GIDs 0–31).
  // extra_tiles is the comma-separated row string for the single layer row.
  fs::path make_single_tileset_map(const fs::path &dir, std::string_view extra_tiles = "0,0") {
    const auto ts_path = dir / "tileset_a.json";
    const auto map_path = dir / "map.json";
    write_file(ts_path, TILESET_A_JSON);

    std::string content;
    content += R"({"id":"test","tilesets":[{"first_gid":0,"source":")";
    content += ts_path.string();
    content += R"("}],"width":2,"height":1,"layers":[{"name":"ground","tiles":[")";
    content += std::string(extra_tiles);
    content += R"("]}]})";
    write_file(map_path, content);
    return map_path;
  }

} // namespace

// ── find_tileset unit tests ───────────────────────────────────────────────────

TEST_CASE("find_tileset — k_empty_tile returns nullptr") {
  std::vector<corundum::gameplay::world::tilemap::TilemapTileset> tilesets(1);
  tilesets[0].first_gid = 0;
  tilesets[0].tile_count = 32;
  CHECK(corundum::gameplay::world::tilemap::find_tileset(tilesets, corundum::gameplay::world::tilemap::k_empty_tile) ==
        nullptr);
}

TEST_CASE("find_tileset — single tileset, valid GID") {
  std::vector<corundum::gameplay::world::tilemap::TilemapTileset> tilesets(1);
  tilesets[0].first_gid = 0;
  tilesets[0].tile_count = 32;
  const auto *result = corundum::gameplay::world::tilemap::find_tileset(tilesets, 5);
  REQUIRE(result != nullptr);
  CHECK(result->first_gid == 0);
}

TEST_CASE("find_tileset — two tilesets, GID routes correctly") {
  std::vector<corundum::gameplay::world::tilemap::TilemapTileset> tilesets(2);
  tilesets[0].first_gid = 0;
  tilesets[0].tile_count = 32;
  tilesets[1].first_gid = 32;
  tilesets[1].tile_count = 16;

  CHECK(corundum::gameplay::world::tilemap::find_tileset(tilesets, 0) == &tilesets[0]);
  CHECK(corundum::gameplay::world::tilemap::find_tileset(tilesets, 31) == &tilesets[0]);
  CHECK(corundum::gameplay::world::tilemap::find_tileset(tilesets, 32) == &tilesets[1]);
  CHECK(corundum::gameplay::world::tilemap::find_tileset(tilesets, 47) == &tilesets[1]);
  CHECK(corundum::gameplay::world::tilemap::find_tileset(tilesets, 48) == nullptr); // out of range
}

// ── Regression: single-tileset map loads correctly ───────────────────────────

TEST_CASE("load_tilemap — single tileset map") {
  const auto dir = temp_dir("single");
  const auto map_path = make_single_tileset_map(dir);

  auto tm_result = corundum::gameplay::world::tilemap::load_tilemap(map_path);
  REQUIRE(tm_result.has_value());
  const auto &m = *tm_result;
  CHECK(m.width == 2);
  CHECK(m.height == 1);
  REQUIRE(m.tilesets.size() == 1);
  CHECK(m.tilesets[0].first_gid == 0);
  CHECK(m.tilesets[0].tile_count == 32);
  CHECK(m.tilesets[0].info.columns == 8);
  REQUIRE(m.layers.size() == 1);
  CHECK(m.layers[0].name == "ground");
  CHECK(m.layers[0].tiles[0] == 0);
  CHECK(m.layers[0].tiles[1] == 0);
}

// ── Multi-tileset map ─────────────────────────────────────────────────────────

TEST_CASE("load_tilemap — two tilesets, correct first_gid and tile_count") {
  const auto dir = temp_dir("multi");
  const auto ts_a = dir / "ts_a.json";
  const auto ts_b = dir / "ts_b.json";
  write_file(ts_a, TILESET_A_JSON); // 8×4 = 32 tiles
  write_file(ts_b, TILESET_B_JSON); // 4×4 = 16 tiles

  std::string content;
  content += R"({"id":"multi","tilesets":[{"first_gid":0,"source":")";
  content += ts_a.string();
  content += R"("},{"first_gid":32,"source":")";
  content += ts_b.string();
  content += R"("}],"width":2,"height":1,"layers":[{"name":"base","tiles":["0,32"]}]})";
  const auto map_path = dir / "map.json";
  write_file(map_path, content);

  auto tm_result = corundum::gameplay::world::tilemap::load_tilemap(map_path);
  REQUIRE(tm_result.has_value());
  const auto &m = *tm_result;
  REQUIRE(m.tilesets.size() == 2);
  CHECK(m.tilesets[0].first_gid == 0);
  CHECK(m.tilesets[0].tile_count == 32);
  CHECK(m.tilesets[1].first_gid == 32);
  CHECK(m.tilesets[1].tile_count == 16);
  CHECK(m.layers[0].tiles[0] == 0);
  CHECK(m.layers[0].tiles[1] == 32);
}

TEST_CASE("load_tilemap — tilesets are sorted by first_gid even if JSON is unordered") {
  const auto dir = temp_dir("sort");
  const auto ts_a = dir / "ts_a.json";
  const auto ts_b = dir / "ts_b.json";
  write_file(ts_a, TILESET_A_JSON);
  write_file(ts_b, TILESET_B_JSON);

  // Provide tilesets in reverse order in JSON
  std::string content;
  content += R"({"id":"sort","tilesets":[{"first_gid":32,"source":")";
  content += ts_b.string();
  content += R"("},{"first_gid":0,"source":")";
  content += ts_a.string();
  content += R"("}],"width":1,"height":1,"layers":[{"name":"base","tiles":["0"]}]})";
  const auto map_path = dir / "map.json";
  write_file(map_path, content);

  auto tm_result = corundum::gameplay::world::tilemap::load_tilemap(map_path);
  REQUIRE(tm_result.has_value());
  const auto &m = *tm_result;
  REQUIRE(m.tilesets.size() == 2);
  CHECK(m.tilesets[0].first_gid == 0);
  CHECK(m.tilesets[1].first_gid == 32);
}

// ── k_empty_tile accepted in dense layers ──────────────────────────────────────

TEST_CASE("load_tilemap — k_empty_tile (65535) in dense layer is accepted") {
  const auto dir = temp_dir("empty_tile");
  const auto map_path = make_single_tileset_map(dir, "0,65535");
  auto tm_result = corundum::gameplay::world::tilemap::load_tilemap(map_path);
  REQUIRE(tm_result.has_value());
  const auto &m = *tm_result;
  CHECK(m.layers[0].tiles[1] == corundum::gameplay::world::tilemap::k_empty_tile);
}

// ── Error: old "tileset" string key rejected ─────────────────────────────────

TEST_CASE("load_tilemap — old 'tileset' string key throws") {
  const auto dir = temp_dir("old_format");
  const auto ts_path = dir / "ts.json";
  const auto map_path = dir / "map.json";
  write_file(ts_path, TILESET_A_JSON);

  std::string content;
  content += R"({"id":"old","tileset":")";
  content += ts_path.string();
  content += R"(","width":1,"height":1,"layers":[{"name":"g","tiles":["0"]}]})";
  write_file(map_path, content);

  auto result = corundum::gameplay::world::tilemap::load_tilemap(map_path);
  CHECK(!result.has_value());
}

// ── Error: first_gid not 0 ────────────────────────────────────────────────────

TEST_CASE("load_tilemap — first_gid not starting at 0 throws") {
  const auto dir = temp_dir("no_zero");
  const auto ts_path = dir / "ts.json";
  const auto map_path = dir / "map.json";
  write_file(ts_path, TILESET_A_JSON);

  std::string content;
  content += R"({"id":"nozero","tilesets":[{"first_gid":10,"source":")";
  content += ts_path.string();
  content += R"("}],"width":1,"height":1,"layers":[{"name":"g","tiles":["10"]}]})";
  write_file(map_path, content);

  auto result = corundum::gameplay::world::tilemap::load_tilemap(map_path);
  CHECK(!result.has_value());
}

// ── Error: gap between tilesets ───────────────────────────────────────────────

TEST_CASE("load_tilemap — gap between tilesets throws") {
  const auto dir = temp_dir("gap");
  const auto ts_a = dir / "ts_a.json";
  const auto ts_b = dir / "ts_b.json";
  write_file(ts_a, TILESET_A_JSON); // 32 tiles
  write_file(ts_b, TILESET_B_JSON);

  // first_gid=33 leaves a one-tile gap after tileset A (which covers GIDs 0–31)
  std::string content;
  content += R"({"id":"gap","tilesets":[{"first_gid":0,"source":")";
  content += ts_a.string();
  content += R"("},{"first_gid":33,"source":")";
  content += ts_b.string();
  content += R"("}],"width":1,"height":1,"layers":[{"name":"g","tiles":["0"]}]})";
  const auto map_path = dir / "map.json";
  write_file(map_path, content);

  auto result = corundum::gameplay::world::tilemap::load_tilemap(map_path);
  CHECK(!result.has_value());
}

// ── Error: duplicate first_gid ────────────────────────────────────────────────

TEST_CASE("load_tilemap — duplicate first_gid throws") {
  const auto dir = temp_dir("dup");
  const auto ts_a = dir / "ts_a.json";
  const auto ts_b = dir / "ts_b.json";
  write_file(ts_a, TILESET_A_JSON);
  write_file(ts_b, TILESET_B_JSON);

  std::string content;
  content += R"({"id":"dup","tilesets":[{"first_gid":0,"source":")";
  content += ts_a.string();
  content += R"("},{"first_gid":0,"source":")";
  content += ts_b.string();
  content += R"("}],"width":1,"height":1,"layers":[{"name":"g","tiles":["0"]}]})";
  const auto map_path = dir / "map.json";
  write_file(map_path, content);

  auto result = corundum::gameplay::world::tilemap::load_tilemap(map_path);
  CHECK(!result.has_value());
}

// ── Error: GID in layer not covered by any tileset ───────────────────────────

TEST_CASE("load_tilemap — GID in layer beyond tileset range throws") {
  const auto dir = temp_dir("gid_range");
  // TILESET_A has 32 tiles (GIDs 0–31); GID 32 is out of range
  const auto map_path = make_single_tileset_map(dir, "0,32");
  auto result = corundum::gameplay::world::tilemap::load_tilemap(map_path);
  CHECK(!result.has_value());
}

// ── Collisions ────────────────────────────────────────────────────────────────

TEST_CASE("load_tilemap — absent collisions key produces empty vector") {
  const auto dir = temp_dir("col_absent");
  const auto map_path = make_single_tileset_map(dir);
  auto tm_result = corundum::gameplay::world::tilemap::load_tilemap(map_path);
  REQUIRE(tm_result.has_value());
  const auto &m = *tm_result;
  CHECK(m.collisions.size() == 0);
}

TEST_CASE("load_tilemap — collisions array loads correctly") {
  const auto dir = temp_dir("col_load");
  const auto ts_path = dir / "tileset_a.json";
  const auto map_path = dir / "map.json";
  write_file(ts_path, TILESET_A_JSON);

  std::string content;
  content += R"({"id":"t","tilesets":[{"first_gid":0,"source":")";
  content += ts_path.string();
  content +=
      R"("}],"width":2,"height":1,"layers":[{"name":"g","tiles":["0,0"]}],"collisions":[{"x":10.0,"y":20.0,"w":30.0,"h":40.0},{"x":1.5,"y":2.5,"w":8.0,"h":4.0}]})";
  write_file(map_path, content);

  auto tm_result = corundum::gameplay::world::tilemap::load_tilemap(map_path);
  REQUIRE(tm_result.has_value());
  const auto &m = *tm_result;
  REQUIRE(m.collisions.size() == 2);
  CHECK(m.collisions.xs[0] == doctest::Approx(10.f));
  CHECK(m.collisions.ys[0] == doctest::Approx(20.f));
  CHECK(m.collisions.ws[0] == doctest::Approx(30.f));
  CHECK(m.collisions.hs[0] == doctest::Approx(40.f));
  CHECK(m.collisions.xs[1] == doctest::Approx(1.5f));
  CHECK(m.collisions.ys[1] == doctest::Approx(2.5f));
}

TEST_CASE("load_tilemap — collision with zero width throws") {
  const auto dir = temp_dir("col_zero_w");
  const auto ts_path = dir / "tileset_a.json";
  const auto map_path = dir / "map.json";
  write_file(ts_path, TILESET_A_JSON);

  std::string content;
  content += R"({"id":"t","tilesets":[{"first_gid":0,"source":")";
  content += ts_path.string();
  content +=
      R"("}],"width":2,"height":1,"layers":[{"name":"g","tiles":["0,0"]}],"collisions":[{"x":0,"y":0,"w":0,"h":16}]})";
  write_file(map_path, content);

  auto result = corundum::gameplay::world::tilemap::load_tilemap(map_path);
  CHECK(!result.has_value());
}

TEST_CASE("load_tilemap — collision with zero height throws") {
  const auto dir = temp_dir("col_zero_h");
  const auto ts_path = dir / "tileset_a.json";
  const auto map_path = dir / "map.json";
  write_file(ts_path, TILESET_A_JSON);

  std::string content;
  content += R"({"id":"t","tilesets":[{"first_gid":0,"source":")";
  content += ts_path.string();
  content +=
      R"("}],"width":2,"height":1,"layers":[{"name":"g","tiles":["0,0"]}],"collisions":[{"x":0,"y":0,"w":16,"h":0}]})";
  write_file(map_path, content);

  auto result = corundum::gameplay::world::tilemap::load_tilemap(map_path);
  CHECK(!result.has_value());
}

// ── Layer z_index ─────────────────────────────────────────────────────────────

TEST_CASE("load_tilemap — absent z_index defaults to 0") {
  const auto dir = temp_dir("zidx_absent");
  const auto map_path = make_single_tileset_map(dir);
  auto tm_result = corundum::gameplay::world::tilemap::load_tilemap(map_path);
  REQUIRE(tm_result.has_value());
  const auto &m = *tm_result;
  CHECK(m.layers[0].z_index == 0);
}

TEST_CASE("load_tilemap — z_index is loaded when present") {
  const auto dir = temp_dir("zidx_present");
  const auto ts_path = dir / "tileset_a.json";
  const auto map_path = dir / "map.json";
  write_file(ts_path, TILESET_A_JSON);

  std::string content;
  content += R"({"id":"t","tilesets":[{"first_gid":0,"source":")";
  content += ts_path.string();
  content += R"("}],"width":1,"height":1,"layers":[)";
  content += R"({"name":"ground","tiles":["0"]},)";
  content += R"({"name":"canopy","z_index":2,"tiles":["0"]}]})";
  write_file(map_path, content);

  auto tm_result = corundum::gameplay::world::tilemap::load_tilemap(map_path);
  REQUIRE(tm_result.has_value());
  const auto &m = *tm_result;
  REQUIRE(m.layers.size() == 2);
  CHECK(m.layers[0].z_index == 0);
  CHECK(m.layers[1].z_index == 2);
}

TEST_CASE("load_tilemap — negative z_index is clamped to 0") {
  const auto dir = temp_dir("zidx_neg");
  const auto ts_path = dir / "tileset_a.json";
  const auto map_path = dir / "map.json";
  write_file(ts_path, TILESET_A_JSON);

  std::string content;
  content += R"({"id":"t","tilesets":[{"first_gid":0,"source":")";
  content += ts_path.string();
  content += R"("}],"width":1,"height":1,"layers":[{"name":"ground","z_index":-3,"tiles":["0"]}]})";
  write_file(map_path, content);

  auto tm_result = corundum::gameplay::world::tilemap::load_tilemap(map_path);
  REQUIRE(tm_result.has_value());
  const auto &m = *tm_result;
  CHECK(m.layers[0].z_index == 0);
}

TEST_CASE("load_tilemap — collision element missing field throws") {
  const auto dir = temp_dir("col_missing");
  const auto ts_path = dir / "tileset_a.json";
  const auto map_path = dir / "map.json";
  write_file(ts_path, TILESET_A_JSON);

  std::string content;
  content += R"({"id":"t","tilesets":[{"first_gid":0,"source":")";
  content += ts_path.string();
  content +=
      R"("}],"width":2,"height":1,"layers":[{"name":"g","tiles":["0,0"]}],"collisions":[{"y":0,"w":16,"h":16}]})";
  write_file(map_path, content);

  auto result = corundum::gameplay::world::tilemap::load_tilemap(map_path);
  CHECK(!result.has_value());
}
