#include <doctest/doctest.h>

#include <corundum/gameplay/world/tilemap/loader.hpp>
#include <corundum/gameplay/world/tilemap/tilemap.hpp>

#include <filesystem>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>
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

  // Builds a spritepacker atlas JSON (schema_version 2) with `count` sprites named "tile_0".."tile_
  // {count-1}", each tile_w x tile_h, laid out in a single non-overlapping row — geometry doesn't
  // need to be visually valid, just structurally valid for the loader. No trim, bottom-center pivot
  // (spritepacker's default), matching what a real pack run would emit for untrimmed square tiles.
  std::string make_atlas_json(const std::string &path, int count, int tile_w, int tile_h) {
    std::string sprites;
    for (int i = 0; i < count; ++i) {
      if (i > 0)
        sprites += ",";
      sprites += std::format(R"({{"name":"tile_{}","x":{},"y":0,"w":{},"h":{},)"
                             R"("trim_x":0,"trim_y":0,"source_width":{},"source_height":{},)"
                             R"("pivot_x":0.5,"pivot_y":1.0}})",
                             i, i * tile_w, tile_w, tile_h, tile_w, tile_h);
    }
    return std::format(R"({{"schema_version":2,"path":"{}","width":{},"height":{},"sprites":[{}]}})", path,
                       count * tile_w, tile_h, sprites);
  }

  std::string tileset_a_json() {
    return make_atlas_json("game/assets/textures/tileset.png", 32, 16, 16);
  }

  std::string tileset_b_json() {
    return make_atlas_json("game/assets/textures/tileset2.png", 16, 16, 16);
  }

  // Builds a minimal single-tileset 2×1 map using tileset A (32 tiles, GIDs 0–31).
  // extra_tiles is the comma-separated row string for the single layer row.
  fs::path make_single_tileset_map(const fs::path &dir, std::string_view extra_tiles = "0,0") {
    const auto ts_path = dir / "tileset_a.json";
    const auto map_path = dir / "map.json";
    write_file(ts_path, tileset_a_json());

    std::string content;
    content += R"({"id":"test","tilesets":[{"first_gid":0,"source":")";
    content += ts_path.string();
    content += R"("}],"width":2,"height":1,"layers":[{"name":"ground","tiles":[")";
    content += std::string(extra_tiles);
    content += R"("]}]})";
    write_file(map_path, content);
    return map_path;
  }

  /// Builds a 2x1 single-tileset map with an explicit schema_version.
  fs::path make_schema_map(const fs::path &dir, int version) {
    const auto ts_path = dir / "tileset_a.json";
    const auto map_path = dir / "map.json";
    write_file(ts_path, tileset_a_json());

    std::string content;
    content += std::format(R"({{"id":"test","schema_version":{},"tilesets":[{{"first_gid":0,"source":")", version);
    content += ts_path.string();
    content += R"("}],"width":2,"height":1,"layers":[{"name":"ground","tiles":["0,0"]}]})";
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
  write_file(ts_a, tileset_a_json()); // 8×4 = 32 tiles
  write_file(ts_b, tileset_b_json()); // 4×4 = 16 tiles

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
  write_file(ts_a, tileset_a_json());
  write_file(ts_b, tileset_b_json());

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
  write_file(ts_path, tileset_a_json());

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
  write_file(ts_path, tileset_a_json());

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
  write_file(ts_a, tileset_a_json()); // 32 tiles
  write_file(ts_b, tileset_b_json());

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
  write_file(ts_a, tileset_a_json());
  write_file(ts_b, tileset_b_json());

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
  write_file(ts_path, tileset_a_json());

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
  CHECK(m.collisions.cols[0] == doctest::Approx(10.f));
  CHECK(m.collisions.rows[0] == doctest::Approx(20.f));
  CHECK(m.collisions.col_spans[0] == doctest::Approx(30.f));
  CHECK(m.collisions.row_spans[0] == doctest::Approx(40.f));
  CHECK(m.collisions.cols[1] == doctest::Approx(1.5f));
  CHECK(m.collisions.rows[1] == doctest::Approx(2.5f));
}

TEST_CASE("load_tilemap — collision with zero width throws") {
  const auto dir = temp_dir("col_zero_w");
  const auto ts_path = dir / "tileset_a.json";
  const auto map_path = dir / "map.json";
  write_file(ts_path, tileset_a_json());

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
  write_file(ts_path, tileset_a_json());

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
  write_file(ts_path, tileset_a_json());

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
  write_file(ts_path, tileset_a_json());

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
  write_file(ts_path, tileset_a_json());

  std::string content;
  content += R"({"id":"t","tilesets":[{"first_gid":0,"source":")";
  content += ts_path.string();
  content +=
      R"("}],"width":2,"height":1,"layers":[{"name":"g","tiles":["0,0"]}],"collisions":[{"y":0,"w":16,"h":16}]})";
  write_file(map_path, content);

  auto result = corundum::gameplay::world::tilemap::load_tilemap(map_path);
  CHECK(!result.has_value());
}

// ── schema_version ────────────────────────────────────────────────────────────

TEST_CASE("load_tilemap — absent schema_version is treated as version 1 and loads fine") {
  const auto dir = temp_dir("schema_absent");
  const auto map_path = make_single_tileset_map(dir);

  auto result = corundum::gameplay::world::tilemap::load_tilemap(map_path);
  CHECK(result.has_value());
}

TEST_CASE("load_tilemap — explicit current schema_version loads fine") {
  const auto map_path =
      make_schema_map(temp_dir("schema_current"), corundum::gameplay::world::tilemap::k_tilemap_schema_version);
  auto result = corundum::gameplay::world::tilemap::load_tilemap(map_path);
  CHECK(result.has_value());
}

TEST_CASE("load_tilemap — schema_version newer than supported throws") {
  const auto map_path =
      make_schema_map(temp_dir("schema_future"), corundum::gameplay::world::tilemap::k_tilemap_schema_version + 1);
  auto result = corundum::gameplay::world::tilemap::load_tilemap(map_path);
  CHECK(!result.has_value());
}

TEST_CASE("load_tilemap — schema_version zero (invalid) throws") {
  const auto map_path = make_schema_map(temp_dir("schema_zero"), 0);
  auto result = corundum::gameplay::world::tilemap::load_tilemap(map_path);
  CHECK(!result.has_value());
}

TEST_CASE("load_tilemap — schema_version wrong type throws") {
  const auto dir = temp_dir("schema_wrong_type");
  const auto ts_path = dir / "tileset_a.json";
  const auto map_path = dir / "map.json";
  write_file(ts_path, tileset_a_json());

  std::string content;
  content += R"({"id":"test","schema_version":"1","tilesets":[{"first_gid":0,"source":")";
  content += ts_path.string();
  content += R"("}],"width":2,"height":1,"layers":[{"name":"ground","tiles":["0,0"]}]})";
  write_file(map_path, content);

  auto result = corundum::gameplay::world::tilemap::load_tilemap(map_path);
  CHECK(!result.has_value());
}

// ── terrain-material tag ─────────────────────────────────────────────────────

TEST_CASE("load_tileset — 'material' field is loaded") {
  const auto dir = temp_dir("tileset_material");
  const auto ts_path = dir / "tileset_a.json";
  const auto atlas = R"({"schema_version":2,"path":"game/assets/textures/tileset.png","width":16,"height":16,)"
                     R"("sprites":[{"name":"tile_0","x":0,"y":0,"w":16,"h":16}]})";
  write_file(ts_path, atlas);
  std::ifstream f(ts_path);
  nlohmann::json j = nlohmann::json::parse(f);
  j["material"] = "stone";
  write_file(ts_path, j.dump());

  auto result = corundum::gameplay::world::tilemap::load_tileset(ts_path);
  REQUIRE(result.has_value());
  CHECK(result->material == "stone");
}

TEST_CASE("load_tileset — absent 'material' field defaults to empty string") {
  const auto dir = temp_dir("tileset_material_absent");
  const auto ts_path = dir / "tileset_a.json";
  write_file(ts_path, tileset_a_json());

  auto result = corundum::gameplay::world::tilemap::load_tileset(ts_path);
  REQUIRE(result.has_value());
  CHECK(result->material.empty());
}

// ── per-tile pivot (sourced from spritepacker's own pivot_x/pivot_y per sprite) ────────────────

TEST_CASE("load_tileset — per-tile pivot is converted from spritepacker's trimmed-box, raster-y "
          "convention into full-frame, bottom-origin") {
  const auto dir = temp_dir("tileset_pivot");
  const auto ts_path = dir / "tileset_a.json";
  // No trim (trim_x/trim_y=0, source == trimmed size), so the conversion collapses to just
  // pivot_x unchanged and pivot_y inverted (spritepacker: 1.0 = bottom; this engine: 0.0 = bottom).
  write_file(ts_path, R"({"schema_version":2,"path":"game/assets/textures/tileset.png","width":128,"height":256,)"
                      R"("sprites":[{"name":"cliff","x":0,"y":0,"w":128,"h":256,)"
                      R"("trim_x":0,"trim_y":0,"source_width":128,"source_height":256,)"
                      R"("pivot_x":0.5,"pivot_y":0.57}]})");

  auto result = corundum::gameplay::world::tilemap::load_tileset(ts_path);
  REQUIRE(result.has_value());
  const auto pivot = corundum::gameplay::world::tilemap::get_tile_pivot(*result, 0);
  CHECK(pivot.x == doctest::Approx(0.5));
  CHECK(pivot.y == doctest::Approx(0.43));
}

TEST_CASE("load_tileset — spritepacker's default bottom-center pivot (0.5, 1.0) converts to (0.5, 0.0)") {
  const auto dir = temp_dir("tileset_pivot_default");
  const auto ts_path = dir / "tileset_a.json";
  write_file(ts_path, tileset_a_json()); // fixture sprites all use pivot_x=0.5, pivot_y=1.0

  auto result = corundum::gameplay::world::tilemap::load_tileset(ts_path);
  REQUIRE(result.has_value());
  const auto pivot = corundum::gameplay::world::tilemap::get_tile_pivot(*result, 0);
  CHECK(pivot.x == doctest::Approx(0.5));
  CHECK(pivot.y == doctest::Approx(0.0));
}

TEST_CASE("load_tileset — get_tile_pivot on an out-of-range local_id returns the {0.5,0} default") {
  const auto dir = temp_dir("tileset_pivot_oob_lookup");
  const auto ts_path = dir / "tileset_a.json";
  write_file(ts_path, tileset_a_json());

  auto result = corundum::gameplay::world::tilemap::load_tileset(ts_path);
  REQUIRE(result.has_value());
  const auto pivot = corundum::gameplay::world::tilemap::get_tile_pivot(*result, 9999);
  CHECK(pivot.x == doctest::Approx(0.5));
  CHECK(pivot.y == doctest::Approx(0.0));
}

// ── per-tile trim/frame offset ──────────────────────────────────────────────────────────────

TEST_CASE("load_tileset — trim_x/trim_y/source_width/source_height are loaded and resolved via "
          "get_tile_frame_offset") {
  const auto dir = temp_dir("tileset_trims");
  const auto ts_path = dir / "tileset_a.json";
  write_file(ts_path, R"({"schema_version":2,"path":"game/assets/textures/tileset.png","width":132,"height":71,)"
                      R"("sprites":[{"name":"a","x":0,"y":0,"w":132,"h":71,)"
                      R"("trim_x":62,"trim_y":93,"source_width":256,"source_height":256}]})");

  auto result = corundum::gameplay::world::tilemap::load_tileset(ts_path);
  REQUIRE(result.has_value());
  const auto frame = corundum::gameplay::world::tilemap::get_tile_frame_offset(*result, 0);
  CHECK(frame.trim_x == 62);
  CHECK(frame.trim_y == 93);
  CHECK(frame.full_width == 256);
  CHECK(frame.full_height == 256);
}

TEST_CASE("load_tileset — sprite omitting trim_x/trim_y/source_width/source_height defaults sensibly") {
  const auto dir = temp_dir("tileset_trims_absent");
  const auto ts_path = dir / "tileset_a.json";
  write_file(ts_path, R"({"schema_version":2,"path":"game/assets/textures/tileset.png","width":16,"height":16,)"
                      R"("sprites":[{"name":"a","x":0,"y":0,"w":16,"h":16}]})");

  auto result = corundum::gameplay::world::tilemap::load_tileset(ts_path);
  REQUIRE(result.has_value());
  const auto frame = corundum::gameplay::world::tilemap::get_tile_frame_offset(*result, 0);
  CHECK(frame.trim_x == 0);
  CHECK(frame.trim_y == 0);
  CHECK(frame.full_width == 16); // defaults to w/h when source_width/height are absent
  CHECK(frame.full_height == 16);
}

TEST_CASE("tile_source_rect — returns the atlas rect directly (already trimmed and packed, no grid "
          "cell math)") {
  const auto dir = temp_dir("tileset_source_rect");
  const auto ts_path = dir / "tileset_a.json";
  const auto map_path = dir / "map.json";
  write_file(ts_path, R"({"schema_version":2,"path":"game/assets/textures/tileset.png","width":512,"height":256,)"
                      R"("sprites":[)"
                      R"({"name":"tile_0","x":0,"y":0,"w":16,"h":16},)"
                      R"({"name":"tile_1","x":318,"y":93,"w":132,"h":71}]})");

  std::string content;
  content += R"({"id":"test","tilesets":[{"first_gid":0,"source":")";
  content += ts_path.string();
  content += R"("}],"width":2,"height":1,"layers":[{"name":"ground","tiles":["0,1"]}]})";
  write_file(map_path, content);

  auto result = corundum::gameplay::world::tilemap::load_tilemap(map_path);
  REQUIRE(result.has_value());
  const auto *ts = corundum::gameplay::world::tilemap::find_tileset(result->tilesets, 1);
  REQUIRE(ts != nullptr);
  const auto rect = corundum::gameplay::world::tilemap::tile_source_rect(*ts, 1);
  CHECK(rect.x == 318);
  CHECK(rect.y == 93);
  CHECK(rect.width == 132);
  CHECK(rect.height == 71);
}

TEST_CASE("load_tilemap — layer 'material_overrides' is loaded") {
  const auto dir = temp_dir("material_overrides");
  const auto ts_path = dir / "tileset_a.json";
  const auto map_path = dir / "map.json";
  write_file(ts_path, tileset_a_json());

  std::string content;
  content += R"({"id":"test","tilesets":[{"first_gid":0,"source":")";
  content += ts_path.string();
  content += R"("}],"width":2,"height":1,"layers":[{"name":"ground","tiles":["0,0"],)"
             R"("material_overrides":[{"col":1,"row":0,"material":"snow"}]}]})";
  write_file(map_path, content);

  auto result = corundum::gameplay::world::tilemap::load_tilemap(map_path);
  REQUIRE(result.has_value());
  const auto &layer = result->layers[0];
  REQUIRE(layer.material_overrides.contains(1)); // row 0 * width 2 + col 1
  CHECK(layer.material_overrides.at(1) == "snow");
}

TEST_CASE("load_tilemap — absent 'material_overrides' produces an empty map") {
  const auto dir = temp_dir("material_overrides_absent");
  const auto map_path = make_single_tileset_map(dir);

  auto result = corundum::gameplay::world::tilemap::load_tilemap(map_path);
  REQUIRE(result.has_value());
  CHECK(result->layers[0].material_overrides.empty());
}

TEST_CASE("load_tilemap — material_overrides entry out of bounds throws") {
  const auto dir = temp_dir("material_overrides_oob");
  const auto ts_path = dir / "tileset_a.json";
  const auto map_path = dir / "map.json";
  write_file(ts_path, tileset_a_json());

  std::string content;
  content += R"({"id":"test","tilesets":[{"first_gid":0,"source":")";
  content += ts_path.string();
  content += R"("}],"width":2,"height":1,"layers":[{"name":"ground","tiles":["0,0"],)"
             R"("material_overrides":[{"col":5,"row":0,"material":"snow"}]}]})";
  write_file(map_path, content);

  auto result = corundum::gameplay::world::tilemap::load_tilemap(map_path);
  CHECK(!result.has_value());
}

TEST_CASE("load_tilemap — material_overrides entry missing 'material' throws") {
  const auto dir = temp_dir("material_overrides_missing_field");
  const auto ts_path = dir / "tileset_a.json";
  const auto map_path = dir / "map.json";
  write_file(ts_path, tileset_a_json());

  std::string content;
  content += R"({"id":"test","tilesets":[{"first_gid":0,"source":")";
  content += ts_path.string();
  content += R"("}],"width":2,"height":1,"layers":[{"name":"ground","tiles":["0,0"],)"
             R"("material_overrides":[{"col":1,"row":0}]}]})";
  write_file(map_path, content);

  auto result = corundum::gameplay::world::tilemap::load_tilemap(map_path);
  CHECK(!result.has_value());
}

// ── Sidecar ────────────────────────────────────────────────────────────────────

TEST_CASE("load_tileset — sidecar overrides material, footprints, pivots") {
  const auto dir = temp_dir("tileset_sidecar");
  const auto ts_path = dir / "tileset_a.json";
  write_file(ts_path, tileset_a_json()); // atlas uses pivot (0.5, 1.0), no material, no footprints

  // Build a sidecar with authoring overrides
  const auto sidecar_path = ts_path.parent_path() / (ts_path.stem().string() + ".tiledata.json");
  const std::string sidecar = R"({
    "schema_version": 1,
    "material": "stone",
    "tile_footprints": [
      {"name": "tile_0", "w": 2, "h": 3},
      {"name": "tile_1", "w": 1, "h": 1}
    ],
    "pivots": [
      {"name": "tile_0", "pivot_x": 0.3, "pivot_y": 0.2}
    ]
  })";
  write_file(sidecar_path, sidecar);

  auto result = corundum::gameplay::world::tilemap::load_tileset(ts_path);
  REQUIRE(result.has_value());
  const auto &info = *result;

  CHECK(info.material == "stone");

  const auto fp0 = info.tile_footprints.find(0);
  REQUIRE(fp0 != info.tile_footprints.end());
  CHECK(fp0->second.w == 2);
  CHECK(fp0->second.h == 3);

  const auto fp1 = info.tile_footprints.find(1);
  REQUIRE(fp1 != info.tile_footprints.end());
  CHECK(fp1->second.w == 1);
  CHECK(fp1->second.h == 1);

  CHECK(info.tile_pivot_x[0] == doctest::Approx(0.3f));
  CHECK(info.tile_pivot_y[0] == doctest::Approx(0.2f));

  // tile_1 pivot was not overridden — stays at the atlas default (0.5, 0.0 after conversion)
  CHECK(info.tile_pivot_x[1] == doctest::Approx(0.5f));
  CHECK(info.tile_pivot_y[1] == doctest::Approx(0.0f));

  // Unknown sprite names in sidecar are silently skipped — no error, just ignored
  CHECK(info.tile_footprints.find(31) == info.tile_footprints.end()); // not in sidecar
}

TEST_CASE("load_tileset — absent sidecar falls back to atlas fields") {
  const auto dir = temp_dir("tileset_no_sidecar");
  const auto ts_path = dir / "tileset_a.json";
  // Write atlas WITH material baked into it (old convention — no sidecar)
  auto atlas = nlohmann::json::parse(tileset_a_json());
  atlas["material"] = "dirt";
  write_file(ts_path, atlas.dump());

  auto result = corundum::gameplay::world::tilemap::load_tileset(ts_path);
  REQUIRE(result.has_value());
  CHECK(result->material == "dirt");
  CHECK(result->tile_footprints.empty());

  // Pivots come from atlas (spritepacker convention → converted to engine convention)
  CHECK(result->tile_pivot_x[0] == doctest::Approx(0.5f));
  CHECK(result->tile_pivot_y[0] == doctest::Approx(0.0f));
}
