#include <doctest/doctest.h>

#include <corundum/resources/sprite_atlas.hpp>

#include <filesystem>
#include <fstream>
#include <string_view>

namespace fs = std::filesystem;

namespace {

  void write_file(const fs::path &p, std::string_view content) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p);
    f << content;
  }

  fs::path temp_dir(std::string_view tag) {
    const auto p = fs::temp_directory_path() / "crpg_test_sprite_atlas" / tag;
    fs::create_directories(p);
    return p;
  }

  constexpr std::string_view VALID_ATLAS_JSON = R"({
    "schema_version": 2,
    "path": "dist/atlases/knight.png",
    "width": 256, "height": 128,
    "sprites": [
      {"name":"knight_walk_south_0","x":10,"y":20,"w":24,"h":40,
       "trim_x":8,"trim_y":16,"source_width":40,"source_height":64,
       "pivot_x":0.5,"pivot_y":1.0},
      {"name":"knight_walk_south_1","x":10,"y":20,"w":24,"h":40,
       "trim_x":8,"trim_y":16,"source_width":40,"source_height":64,
       "pivot_x":0.5,"pivot_y":1.0}
    ]
  })";

} // namespace

TEST_CASE("load_sprite_atlas — valid atlas parses correctly") {
  const auto dir = temp_dir("valid");
  const auto path = dir / "atlas.json";
  write_file(path, VALID_ATLAS_JSON);

  auto result = corundum::resources::load_sprite_atlas(path);
  REQUIRE(result.has_value());
  const auto &atlas = *result;
  CHECK(atlas.path == "dist/atlases/knight.png");
  CHECK(atlas.width == 256);
  CHECK(atlas.height == 128);
  REQUIRE(atlas.sprites.size() == 2);

  const auto &s0 = atlas.sprites[0];
  CHECK(s0.name == "knight_walk_south_0");
  CHECK(s0.x == 10);
  CHECK(s0.y == 20);
  CHECK(s0.w == 24);
  CHECK(s0.h == 40);
  CHECK(s0.trim_x == 8);
  CHECK(s0.trim_y == 16);
  CHECK(s0.source_width == 40);
  CHECK(s0.source_height == 64);
  CHECK(s0.pivot_x == doctest::Approx(0.5f));
  CHECK(s0.pivot_y == doctest::Approx(1.0f));

  const auto &s1 = atlas.sprites[1];
  CHECK(s1.name == "knight_walk_south_1");
  CHECK(s1.x == s0.x);
  CHECK(s1.y == s0.y);
  CHECK(s1.w == s0.w);
  CHECK(s1.h == s0.h);
}

TEST_CASE("load_sprite_atlas — missing 'schema_version' fails") {
  const auto dir = temp_dir("missing_schema_version");
  const auto path = dir / "atlas.json";
  write_file(path, R"({"path":"p.png","width":4,"height":4,"sprites":[]})");

  auto result = corundum::resources::load_sprite_atlas(path);
  CHECK(!result.has_value());
}

TEST_CASE("load_sprite_atlas — 'schema_version' wrong type fails") {
  const auto dir = temp_dir("schema_version_wrong_type");
  const auto path = dir / "atlas.json";
  write_file(path, R"({"schema_version":"2","path":"p.png","width":4,"height":4,"sprites":[]})");

  auto result = corundum::resources::load_sprite_atlas(path);
  CHECK(!result.has_value());
}

TEST_CASE("load_sprite_atlas — 'schema_version' older than expected fails") {
  const auto dir = temp_dir("schema_version_older");
  const auto path = dir / "atlas.json";
  write_file(path, R"({"schema_version":1,"path":"p.png","width":4,"height":4,"sprites":[]})");

  auto result = corundum::resources::load_sprite_atlas(path);
  CHECK(!result.has_value());
}

TEST_CASE("load_sprite_atlas — 'schema_version' newer than expected fails") {
  const auto dir = temp_dir("schema_version_newer");
  const auto path = dir / "atlas.json";
  write_file(path, R"({"schema_version":3,"path":"p.png","width":4,"height":4,"sprites":[]})");

  auto result = corundum::resources::load_sprite_atlas(path);
  CHECK(!result.has_value());
}

TEST_CASE("load_sprite_atlas — malformed JSON fails") {
  const auto dir = temp_dir("malformed");
  const auto path = dir / "atlas.json";
  write_file(path, R"({not valid json)");

  auto result = corundum::resources::load_sprite_atlas(path);
  CHECK(!result.has_value());
}

TEST_CASE("load_sprite_atlas — missing 'path' fails") {
  const auto dir = temp_dir("missing_path");
  const auto path = dir / "atlas.json";
  write_file(path, R"({"schema_version":2,"width":4,"height":4,"sprites":[]})");

  auto result = corundum::resources::load_sprite_atlas(path);
  CHECK(!result.has_value());
}

TEST_CASE("load_sprite_atlas — missing 'width' fails") {
  const auto dir = temp_dir("missing_width");
  const auto path = dir / "atlas.json";
  write_file(path, R"({"schema_version":2,"path":"p.png","height":4,"sprites":[]})");

  auto result = corundum::resources::load_sprite_atlas(path);
  CHECK(!result.has_value());
}

TEST_CASE("load_sprite_atlas — missing 'height' fails") {
  const auto dir = temp_dir("missing_height");
  const auto path = dir / "atlas.json";
  write_file(path, R"({"schema_version":2,"path":"p.png","width":4,"sprites":[]})");

  auto result = corundum::resources::load_sprite_atlas(path);
  CHECK(!result.has_value());
}

TEST_CASE("load_sprite_atlas — 'width' <= 0 fails") {
  const auto dir = temp_dir("width_zero");
  const auto path = dir / "atlas.json";
  write_file(path, R"({"schema_version":2,"path":"p.png","width":0,"height":4,"sprites":[]})");

  auto result = corundum::resources::load_sprite_atlas(path);
  CHECK(!result.has_value());
}

TEST_CASE("load_sprite_atlas — 'height' <= 0 fails") {
  const auto dir = temp_dir("height_zero");
  const auto path = dir / "atlas.json";
  write_file(path, R"({"schema_version":2,"path":"p.png","width":4,"height":0,"sprites":[]})");

  auto result = corundum::resources::load_sprite_atlas(path);
  CHECK(!result.has_value());
}

TEST_CASE("load_sprite_atlas — missing 'sprites' array fails") {
  const auto dir = temp_dir("missing_sprites");
  const auto path = dir / "atlas.json";
  write_file(path, R"({"schema_version":2,"path":"p.png","width":4,"height":4})");

  auto result = corundum::resources::load_sprite_atlas(path);
  CHECK(!result.has_value());
}

TEST_CASE("load_sprite_atlas — 'sprites' present but not an array fails") {
  const auto dir = temp_dir("sprites_not_array");
  const auto path = dir / "atlas.json";
  write_file(path, R"({"schema_version":2,"path":"p.png","width":4,"height":4,"sprites":{}})");

  auto result = corundum::resources::load_sprite_atlas(path);
  CHECK(!result.has_value());
}

TEST_CASE("load_sprite_atlas — empty 'sprites' array is valid") {
  const auto dir = temp_dir("empty_sprites");
  const auto path = dir / "atlas.json";
  write_file(path, R"({"schema_version":2,"path":"p.png","width":4,"height":4,"sprites":[]})");

  auto result = corundum::resources::load_sprite_atlas(path);
  REQUIRE(result.has_value());
  CHECK(result->sprites.empty());
}

TEST_CASE("load_sprite_atlas — sprite missing 'name' fails") {
  const auto dir = temp_dir("sprite_missing_name");
  const auto path = dir / "atlas.json";
  write_file(path,
             R"({"schema_version":2,"path":"p.png","width":4,"height":4,
                 "sprites":[{"x":0,"y":0,"w":4,"h":4}]})");

  auto result = corundum::resources::load_sprite_atlas(path);
  CHECK(!result.has_value());
}

TEST_CASE("load_sprite_atlas — sprite missing 'w' fails") {
  const auto dir = temp_dir("sprite_missing_w");
  const auto path = dir / "atlas.json";
  write_file(path,
             R"({"schema_version":2,"path":"p.png","width":4,"height":4,
                 "sprites":[{"name":"a","x":0,"y":0,"h":4}]})");

  auto result = corundum::resources::load_sprite_atlas(path);
  CHECK(!result.has_value());
}

TEST_CASE("load_sprite_atlas — sprite with empty 'name' fails") {
  const auto dir = temp_dir("sprite_empty_name");
  const auto path = dir / "atlas.json";
  write_file(path,
             R"({"schema_version":2,"path":"p.png","width":4,"height":4,
                 "sprites":[{"name":"","x":0,"y":0,"w":4,"h":4}]})");

  auto result = corundum::resources::load_sprite_atlas(path);
  CHECK(!result.has_value());
}

TEST_CASE("load_sprite_atlas — sprite with 'w' <= 0 fails") {
  const auto dir = temp_dir("sprite_w_zero");
  const auto path = dir / "atlas.json";
  write_file(path,
             R"({"schema_version":2,"path":"p.png","width":4,"height":4,
                 "sprites":[{"name":"a","x":0,"y":0,"w":0,"h":4}]})");

  auto result = corundum::resources::load_sprite_atlas(path);
  CHECK(!result.has_value());
}

TEST_CASE("load_sprite_atlas — sprite with 'h' <= 0 fails") {
  const auto dir = temp_dir("sprite_h_zero");
  const auto path = dir / "atlas.json";
  write_file(path,
             R"({"schema_version":2,"path":"p.png","width":4,"height":4,
                 "sprites":[{"name":"a","x":0,"y":0,"w":4,"h":0}]})");

  auto result = corundum::resources::load_sprite_atlas(path);
  CHECK(!result.has_value());
}

TEST_CASE("load_sprite_atlas — duplicate sprite name fails") {
  const auto dir = temp_dir("duplicate_name");
  const auto path = dir / "atlas.json";
  write_file(path,
             R"({"schema_version":2,"path":"p.png","width":8,"height":8,
                 "sprites":[
                   {"name":"a","x":0,"y":0,"w":4,"h":4},
                   {"name":"a","x":4,"y":4,"w":4,"h":4}
                 ]})");

  auto result = corundum::resources::load_sprite_atlas(path);
  CHECK(!result.has_value());
}

TEST_CASE("load_sprite_atlas — optional sprite fields default correctly when absent") {
  const auto dir = temp_dir("optional_defaults");
  const auto path = dir / "atlas.json";
  write_file(path,
             R"({"schema_version":2,"path":"p.png","width":4,"height":4,
                 "sprites":[{"name":"a","x":1,"y":2,"w":3,"h":4}]})");

  auto result = corundum::resources::load_sprite_atlas(path);
  REQUIRE(result.has_value());
  REQUIRE(result->sprites.size() == 1);
  const auto &sprite = result->sprites[0];
  CHECK(sprite.trim_x == 0);
  CHECK(sprite.trim_y == 0);
  CHECK(sprite.source_width == sprite.w);
  CHECK(sprite.source_height == sprite.h);
  CHECK(sprite.pivot_x == doctest::Approx(0.f));
  CHECK(sprite.pivot_y == doctest::Approx(0.f));
}

TEST_CASE("load_sprite_atlas — non-existent file fails") {
  const auto dir = temp_dir("not_found");
  const auto path = dir / "nonexistent.json";
  auto result = corundum::resources::load_sprite_atlas(path);
  CHECK(!result.has_value());
}
