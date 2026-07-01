#include <doctest/doctest.h>

#include <corundum/resources/character_sheet_loader.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {

  void write_file(const fs::path &p, std::string_view content) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p);
    f << content;
  }

  fs::path temp_dir(std::string_view tag) {
    const auto p = fs::temp_directory_path() / "crpg_test_char_sheet" / tag;
    fs::create_directories(p);
    return p;
  }

  constexpr std::string_view VALID_SHEET_JSON = R"({
    "id":"test_char","path":"game/assets/textures/char.png",
    "frame_width":32,"frame_height":32,
    "frames":{
      "hero":{
        "col_span":1,"row_span":2,
        "walk_around_offset":0.5,"fps":8,
        "collision_w":16,"collision_h":32,
        "south":[{"col":0,"row":0},{"col":1,"row":0}],
        "east":[{"col":2,"row":0}]
      }
    }
  })";

} // namespace

TEST_CASE("load_character_sheet — valid sheet parses correctly") {
  const auto dir = temp_dir("valid");
  const auto path = dir / "sheet.json";
  write_file(path, VALID_SHEET_JSON);

  auto result = corundum::resources::load_character_sheet(path);
  REQUIRE(result.has_value());
  const auto &data = *result;
  CHECK(data.id == "test_char");
  CHECK(data.path == "game/assets/textures/char.png");
  CHECK(data.frame_width == 32);
  CHECK(data.frame_height == 32);
  CHECK(data.offset_x == 0);
  CHECK(data.offset_y == 0);
  CHECK(data.spacing_x == 0);
  CHECK(data.spacing_y == 0);
  REQUIRE(data.sprites.size() == 1);
  const auto &sprite = data.sprites[0];
  CHECK(sprite.name == "hero");
  CHECK(sprite.col_span == 1);
  CHECK(sprite.row_span == 2);
  CHECK(sprite.collision_w == 16);
  CHECK(sprite.collision_h == 32);
  CHECK(sprite.walk_around_offset == doctest::Approx(0.5f));
  CHECK(sprite.fps == doctest::Approx(8.f));
  REQUIRE(sprite.anim_frames[static_cast<uint8_t>(corundum::resources::AnimId::South)].size() == 2);
  CHECK(sprite.anim_frames[static_cast<uint8_t>(corundum::resources::AnimId::East)].size() == 1);
}

TEST_CASE("load_character_sheet — missing required field 'id' fails") {
  const auto dir = temp_dir("missing_id");
  const auto path = dir / "sheet.json";
  write_file(path, R"({"path":"p.png","frame_width":16,"frame_height":16,"frames":{}})");

  auto result = corundum::resources::load_character_sheet(path);
  CHECK(!result.has_value());
}

TEST_CASE("load_character_sheet — missing required field 'path' fails") {
  const auto dir = temp_dir("missing_path");
  const auto path = dir / "sheet.json";
  write_file(path, R"({"id":"x","frame_width":16,"frame_height":16,"frames":{}})");

  auto result = corundum::resources::load_character_sheet(path);
  CHECK(!result.has_value());
}

TEST_CASE("load_character_sheet — missing required field 'frame_width' fails") {
  const auto dir = temp_dir("missing_fw");
  const auto path = dir / "sheet.json";
  write_file(path, R"({"id":"x","path":"p.png","frame_height":16,"frames":{}})");

  auto result = corundum::resources::load_character_sheet(path);
  CHECK(!result.has_value());
}

TEST_CASE("load_character_sheet — missing required field 'frame_height' fails") {
  const auto dir = temp_dir("missing_fh");
  const auto path = dir / "sheet.json";
  write_file(path, R"({"id":"x","path":"p.png","frame_width":16,"frames":{}})");

  auto result = corundum::resources::load_character_sheet(path);
  CHECK(!result.has_value());
}

TEST_CASE("load_character_sheet — col_span < 1 rejected") {
  const auto dir = temp_dir("col_span_zero");
  const auto path = dir / "sheet.json";
  write_file(
      path,
      R"({"id":"x","path":"p.png","frame_width":16,"frame_height":16,"frames":{"s":{"col_span":0,"row_span":1}}})");

  auto result = corundum::resources::load_character_sheet(path);
  CHECK(!result.has_value());
}

TEST_CASE("load_character_sheet — row_span < 1 rejected") {
  const auto dir = temp_dir("row_span_zero");
  const auto path = dir / "sheet.json";
  write_file(
      path,
      R"({"id":"x","path":"p.png","frame_width":16,"frame_height":16,"frames":{"s":{"col_span":1,"row_span":0}}})");

  auto result = corundum::resources::load_character_sheet(path);
  CHECK(!result.has_value());
}

TEST_CASE("load_character_sheet — walk_around_offset defaults to 0.6f when absent") {
  const auto dir = temp_dir("walk_offset_default");
  const auto path = dir / "sheet.json";
  write_file(
      path,
      R"({"id":"x","path":"p.png","frame_width":16,"frame_height":16,"frames":{"s":{"col_span":1,"row_span":1,"south":[{"col":0,"row":0}]}}})");

  auto result = corundum::resources::load_character_sheet(path);
  REQUIRE(result.has_value());
  CHECK(result->sprites[0].walk_around_offset == doctest::Approx(0.6f));
}

TEST_CASE("load_character_sheet — per-frame missing 'col' fails") {
  const auto dir = temp_dir("frame_missing_col");
  const auto path = dir / "sheet.json";
  write_file(
      path,
      R"({"id":"x","path":"p.png","frame_width":16,"frame_height":16,"frames":{"s":{"col_span":1,"row_span":1,"south":[{"row":0}]}}})");

  auto result = corundum::resources::load_character_sheet(path);
  CHECK(!result.has_value());
}

TEST_CASE("load_character_sheet — per-frame missing 'row' fails") {
  const auto dir = temp_dir("frame_missing_row");
  const auto path = dir / "sheet.json";
  write_file(
      path,
      R"({"id":"x","path":"p.png","frame_width":16,"frame_height":16,"frames":{"s":{"col_span":1,"row_span":1,"south":[{"col":0}]}}})");

  auto result = corundum::resources::load_character_sheet(path);
  CHECK(!result.has_value());
}

TEST_CASE("load_character_sheet — missing 'frames' object fails") {
  const auto dir = temp_dir("no_frames");
  const auto path = dir / "sheet.json";
  write_file(path, R"({"id":"x","path":"p.png","frame_width":16,"frame_height":16})");

  auto result = corundum::resources::load_character_sheet(path);
  CHECK(!result.has_value());
}

TEST_CASE("load_character_sheet — empty 'frames' object is valid") {
  const auto dir = temp_dir("empty_frames");
  const auto path = dir / "sheet.json";
  write_file(path, R"({"id":"x","path":"p.png","frame_width":16,"frame_height":16,"frames":{}})");

  auto result = corundum::resources::load_character_sheet(path);
  CHECK(result.has_value());
  CHECK(result->sprites.empty());
}

TEST_CASE("load_character_sheet — non-existent file fails") {
  const auto dir = temp_dir("not_found");
  const auto path = dir / "nonexistent.json";
  auto result = corundum::resources::load_character_sheet(path);
  CHECK(!result.has_value());
}

TEST_CASE("load_character_sheet — malformed JSON fails") {
  const auto dir = temp_dir("malformed");
  const auto path = dir / "sheet.json";
  write_file(path, R"({not valid json)");

  auto result = corundum::resources::load_character_sheet(path);
  CHECK(!result.has_value());
}
