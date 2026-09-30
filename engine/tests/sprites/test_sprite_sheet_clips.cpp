// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include "temp_dir.hpp"
#include <corundum/sprites/sprite_sheet_clips.hpp>

#include <corundum/sprites/sprite_sheet_clips_loader.hpp>
#include <corundum/sprites/sprite_sheet_clips_serializer.hpp>

#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

namespace {

  void write_file(const fs::path &p, std::string_view content) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p);
    f << content;
  }

  corundum::test::TempDir temp_dir(std::string_view tag) {
    return corundum::test::TempDir{"crpg_test_sprite_sheet_clips_", tag};
  }

  /// A minimal valid document: the four required geometry fields plus "path".
  constexpr std::string_view k_minimal_json = R"({
    "frame_width": 16,
    "frame_height": 24,
    "columns": 4,
    "rows": 2,
    "path": "hero.png"
  })";

} // namespace

TEST_CASE("load_sprite_sheet_clips — valid document parses every field") {
  const auto dir = temp_dir("valid");
  const auto path = dir / "sheet.json";
  write_file(path, R"({
    "schema_version": 1,
    "id": "hero",
    "path": "hero.png",
    "columns": 4,
    "rows": 2,
    "frame_width": 16,
    "frame_height": 24,
    "offset_x": 1,
    "offset_y": 2,
    "spacing_x": 3,
    "spacing_y": 4,
    "animations": {
      "fps": 6,
      "clips": [
        {"name": "walk", "frames": [{"col": 0, "row": 0}, {"col": 1, "row": 0}]},
        {"name": "idle", "frames": []}
      ]
    }
  })");

  const auto result = corundum::sprites::load_sprite_sheet_clips(path);
  REQUIRE(result.has_value());
  CHECK(result->id == "hero");
  CHECK(result->path == "hero.png");
  CHECK(result->columns == 4);
  CHECK(result->rows == 2);
  CHECK(result->frame_width == 16);
  CHECK(result->frame_height == 24);
  CHECK(result->offset_x == 1);
  CHECK(result->offset_y == 2);
  CHECK(result->spacing_x == 3);
  CHECK(result->spacing_y == 4);
  CHECK(result->anim_fps == 6);
  REQUIRE(result->clips.size() == 2);
  CHECK(result->clips[0].name == "walk");
  REQUIRE(result->clips[0].frames.size() == 2);
  CHECK(result->clips[0].frames[1].col == 1);
  CHECK(result->clips[1].name == "idle");
  CHECK(result->clips[1].frames.empty());
}

TEST_CASE("load_sprite_sheet_clips — minimal document uses defaults") {
  const auto dir = temp_dir("minimal");
  const auto path = dir / "sheet.json";
  write_file(path, k_minimal_json);

  const auto result = corundum::sprites::load_sprite_sheet_clips(path);
  REQUIRE(result.has_value());
  CHECK(result->id.empty());
  CHECK(result->offset_x == 0);
  CHECK(result->spacing_y == 0);
  CHECK(result->anim_fps == corundum::sprites::k_default_anim_fps);
  CHECK(result->clips.empty());
}

TEST_CASE("load_sprite_sheet_clips — non-existent file fails") {
  const auto dir = temp_dir("not_found");
  const auto path = dir / "nonexistent.json";
  const auto result = corundum::sprites::load_sprite_sheet_clips(path);
  CHECK(!result.has_value());
  CHECK(result.error().find(path.string()) != std::string::npos);
}

TEST_CASE("load_sprite_sheet_clips — malformed JSON fails") {
  const auto dir = temp_dir("malformed_json");
  const auto path = dir / "sheet.json";
  write_file(path, "{ this is not json");

  const auto result = corundum::sprites::load_sprite_sheet_clips(path);
  CHECK(!result.has_value());
  CHECK(result.error().find(path.string()) != std::string::npos);
}

TEST_CASE("load_sprite_sheet_clips — top-level non-object fails without throwing") {
  const auto dir = temp_dir("non_object");
  const auto path = dir / "sheet.json";
  write_file(path, "[1, 2, 3]");

  const auto result = corundum::sprites::load_sprite_sheet_clips(path);
  CHECK(!result.has_value());
}

TEST_CASE("load_sprite_sheet_clips — missing 'schema_version' is treated as version 1") {
  const auto dir = temp_dir("missing_schema_version");
  const auto path = dir / "sheet.json";
  write_file(path, k_minimal_json);

  const auto result = corundum::sprites::load_sprite_sheet_clips(path);
  CHECK(result.has_value());
}

TEST_CASE("load_sprite_sheet_clips — newer 'schema_version' fails") {
  const auto dir = temp_dir("newer_schema_version");
  const auto path = dir / "sheet.json";
  write_file(path, R"({"schema_version":2,"frame_width":16,"frame_height":16,"columns":1,"rows":1,"path":"a.png"})");

  const auto result = corundum::sprites::load_sprite_sheet_clips(path);
  CHECK(!result.has_value());
}

TEST_CASE("load_sprite_sheet_clips — wrong-typed and zero 'schema_version' fail") {
  const auto dir = temp_dir("bad_schema_version");
  const auto path = dir / "sheet.json";
  constexpr std::string_view k_document =
      R"({{"schema_version":{},"frame_width":16,"frame_height":16,"columns":1,"rows":1,"path":"a.png"}})";

  for (const std::string_view version : {R"("1")", "null", "true", "0", "1.5"}) {
    write_file(path, std::format(k_document, version));
    const auto result = corundum::sprites::load_sprite_sheet_clips(path);
    CHECK_MESSAGE(!result.has_value(), "schema_version = ", version);
  }
}

TEST_CASE("load_sprite_sheet_clips — missing or non-string 'path' fails") {
  const auto dir = temp_dir("bad_path");
  const auto path = dir / "sheet.json";

  for (const std::string_view document : {
           R"({"frame_width":16,"frame_height":16,"columns":1,"rows":1})",
           R"({"frame_width":16,"frame_height":16,"columns":1,"rows":1,"path":7})",
       }) {
    write_file(path, document);
    const auto result = corundum::sprites::load_sprite_sheet_clips(path);
    CHECK(!result.has_value());
  }
}

TEST_CASE("load_sprite_sheet_clips — non-string 'id' fails") {
  const auto dir = temp_dir("bad_id");
  const auto path = dir / "sheet.json";
  write_file(path, R"({"id":7,"frame_width":16,"frame_height":16,"columns":1,"rows":1,"path":"a.png"})");

  const auto result = corundum::sprites::load_sprite_sheet_clips(path);
  CHECK(!result.has_value());
}

TEST_CASE("load_sprite_sheet_clips — missing required geometry fails") {
  const auto dir = temp_dir("missing_geometry");
  const auto path = dir / "sheet.json";

  // Each document omits exactly one of the four required geometry fields.
  for (const std::string_view document : {
           R"({"frame_height":16,"columns":1,"rows":1,"path":"a.png"})",
           R"({"frame_width":16,"columns":1,"rows":1,"path":"a.png"})",
           R"({"frame_width":16,"frame_height":16,"rows":1,"path":"a.png"})",
           R"({"frame_width":16,"frame_height":16,"columns":1,"path":"a.png"})",
       }) {
    write_file(path, document);
    const auto result = corundum::sprites::load_sprite_sheet_clips(path);
    CAPTURE(document);
    CHECK(!result.has_value());
  }
}

TEST_CASE("load_sprite_sheet_clips — non-positive frame size fails") {
  const auto dir = temp_dir("bad_frame_size");
  const auto path = dir / "sheet.json";
  constexpr std::string_view k_document =
      R"({{"frame_width":{},"frame_height":16,"columns":1,"rows":1,"path":"a.png"}})";

  for (const std::string_view value : {"0", "-4"}) {
    write_file(path, std::format(k_document, value));
    const auto result = corundum::sprites::load_sprite_sheet_clips(path);
    CHECK_MESSAGE(!result.has_value(), "frame_width = ", value);
  }
}

TEST_CASE("load_sprite_sheet_clips — non-integer geometry fails without throwing") {
  const auto dir = temp_dir("geometry_wrong_type");
  const auto path = dir / "sheet.json";
  constexpr std::string_view k_document =
      R"({{"frame_width":{},"frame_height":16,"columns":1,"rows":1,"path":"a.png"}})";

  for (const std::string_view value : {R"("16")", "null", "true", "16.5", "[16]"}) {
    write_file(path, std::format(k_document, value));
    const auto result = corundum::sprites::load_sprite_sheet_clips(path);
    CHECK_MESSAGE(!result.has_value(), "frame_width = ", value);
  }
}

TEST_CASE("load_sprite_sheet_clips — out-of-range geometry fails") {
  const auto dir = temp_dir("geometry_out_of_range");
  const auto path = dir / "sheet.json";
  // Overflows a 32-bit int if silently narrowed; must be rejected rather than wrapped.
  write_file(path, R"({"frame_width":4294967301,"frame_height":16,"columns":1,"rows":1,"path":"a.png"})");

  const auto result = corundum::sprites::load_sprite_sheet_clips(path);
  CHECK(!result.has_value());
}

TEST_CASE("load_sprite_sheet_clips — negative columns/rows fail") {
  const auto dir = temp_dir("negative_grid");
  const auto path = dir / "sheet.json";

  for (const std::string_view document : {
           R"({"frame_width":16,"frame_height":16,"columns":-1,"rows":1,"path":"a.png"})",
           R"({"frame_width":16,"frame_height":16,"columns":1,"rows":-1,"path":"a.png"})",
       }) {
    write_file(path, document);
    const auto result = corundum::sprites::load_sprite_sheet_clips(path);
    CAPTURE(document);
    CHECK(!result.has_value());
  }
}

TEST_CASE("load_sprite_sheet_clips — negative placement fails") {
  const auto dir = temp_dir("negative_placement");
  const auto path = dir / "sheet.json";
  constexpr std::string_view k_document =
      R"({{"frame_width":16,"frame_height":16,"columns":1,"rows":1,"path":"a.png","{}":-1}})";

  for (const char *key : {"offset_x", "offset_y", "spacing_x", "spacing_y"}) {
    write_file(path, std::format(k_document, key));
    const auto result = corundum::sprites::load_sprite_sheet_clips(path);
    CAPTURE(key);
    CHECK(!result.has_value());
  }
}

TEST_CASE("load_sprite_sheet_clips — non-object 'animations' fails") {
  const auto dir = temp_dir("bad_animations");
  const auto path = dir / "sheet.json";
  write_file(path, R"({"frame_width":16,"frame_height":16,"columns":1,"rows":1,"path":"a.png","animations":[]})");

  const auto result = corundum::sprites::load_sprite_sheet_clips(path);
  CHECK(!result.has_value());
}

TEST_CASE("load_sprite_sheet_clips — non-positive 'fps' fails") {
  const auto dir = temp_dir("bad_fps");
  const auto path = dir / "sheet.json";
  write_file(path,
             R"({"frame_width":16,"frame_height":16,"columns":1,"rows":1,"path":"a.png","animations":{"fps":0}})");

  const auto result = corundum::sprites::load_sprite_sheet_clips(path);
  CHECK(!result.has_value());
}

TEST_CASE("load_sprite_sheet_clips — non-integer 'fps' fails without throwing") {
  const auto dir = temp_dir("fps_wrong_type");
  const auto path = dir / "sheet.json";
  constexpr std::string_view k_document =
      R"({{"frame_width":16,"frame_height":16,"columns":1,"rows":1,"path":"a.png","animations":{{"fps":{}}}}})";

  for (const std::string_view fps : {R"("fast")", "null", "true", "6.5", "[6]"}) {
    write_file(path, std::format(k_document, fps));
    const auto result = corundum::sprites::load_sprite_sheet_clips(path);
    CHECK_MESSAGE(!result.has_value(), "fps = ", fps);
  }
}

TEST_CASE("load_sprite_sheet_clips — non-array 'clips' fails") {
  const auto dir = temp_dir("clips_not_array");
  const auto path = dir / "sheet.json";
  write_file(path,
             R"({"frame_width":16,"frame_height":16,"columns":1,"rows":1,"path":"a.png","animations":{"clips":{}}})");

  const auto result = corundum::sprites::load_sprite_sheet_clips(path);
  CHECK(!result.has_value());
}

TEST_CASE("load_sprite_sheet_clips — empty and duplicate clip names fail") {
  const auto dir = temp_dir("bad_clip_names");
  const auto path = dir / "sheet.json";
  constexpr std::string_view k_document =
      R"({{"frame_width":16,"frame_height":16,"columns":1,"rows":1,"path":"a.png","animations":{{"clips":{}}}}})";

  for (const std::string_view clips : {
           R"([{"name":"","frames":[]}])",
           R"([{"name":"walk","frames":[]},{"name":"walk","frames":[]}])",
           R"([{"name":7,"frames":[]}])",
       }) {
    write_file(path, std::format(k_document, clips));
    const auto result = corundum::sprites::load_sprite_sheet_clips(path);
    CAPTURE(clips);
    CHECK(!result.has_value());
  }
}

TEST_CASE("load_sprite_sheet_clips — malformed frames fail without throwing") {
  const auto dir = temp_dir("bad_frames");
  const auto path = dir / "sheet.json";
  constexpr std::string_view k_document = R"({{"frame_width":16,"frame_height":16,"columns":1,"rows":1,"path":"a.png",)"
                                          R"("animations":{{"clips":[{{"name":"walk","frames":{}}}]}}}})";

  for (const std::string_view frames : {
           R"([{"col":0}])",
           R"([{"row":0}])",
           R"([{"col":"0","row":0}])",
           R"([{"col":0,"row":-1}])",
           R"([7])",
       }) {
    write_file(path, std::format(k_document, frames));
    const auto result = corundum::sprites::load_sprite_sheet_clips(path);
    CAPTURE(frames);
    CHECK(!result.has_value());
  }
}

TEST_CASE("serialize_sprite_sheet_clips — writes the schema version and omits zero placement") {
  using namespace corundum::sprites;
  SpriteSheetClips data;
  data.frame_width = 16;
  data.frame_height = 16;
  data.columns = 2;
  data.rows = 2;
  data.path = "a.png";

  const auto j = serialize_sprite_sheet_clips(data);
  CHECK(j["schema_version"] == k_sprite_sheet_clips_schema_version);
  CHECK(!j.contains("offset_x"));
  CHECK(!j.contains("offset_y"));
  CHECK(!j.contains("spacing_x"));
  CHECK(!j.contains("spacing_y"));
  CHECK(!j.contains("animations"));
}

TEST_CASE("serialize_sprite_sheet_clips — full data round-trips through load") {
  using namespace corundum::sprites;
  const auto dir = temp_dir("roundtrip");
  const auto path = dir / "sheet.json";

  SpriteSheetClips data;
  data.id = "hero";
  data.path = "hero.png";
  data.columns = 4;
  data.rows = 3;
  data.frame_width = 16;
  data.frame_height = 24;
  data.offset_x = 1;
  data.offset_y = 2;
  data.spacing_x = 3;
  data.spacing_y = 4;
  data.anim_fps = 6;
  data.clips.push_back({.name = "walk", .frames = {{0, 0}, {1, 0}, {2, 1}}});
  data.clips.push_back({.name = "idle", .frames = {}});

  write_file(path, serialize_sprite_sheet_clips(data).dump());
  const auto result = load_sprite_sheet_clips(path);
  REQUIRE(result.has_value());
  CHECK(result->id == data.id);
  CHECK(result->path == data.path);
  CHECK(result->columns == data.columns);
  CHECK(result->rows == data.rows);
  CHECK(result->frame_width == data.frame_width);
  CHECK(result->frame_height == data.frame_height);
  CHECK(result->offset_x == data.offset_x);
  CHECK(result->offset_y == data.offset_y);
  CHECK(result->spacing_x == data.spacing_x);
  CHECK(result->spacing_y == data.spacing_y);
  CHECK(result->anim_fps == data.anim_fps);
  REQUIRE(result->clips.size() == 2);
  CHECK(result->clips[0].name == "walk");
  REQUIRE(result->clips[0].frames.size() == 3);
  CHECK(result->clips[0].frames[2].row == 1);
  CHECK(result->clips[1].name == "idle");
  CHECK(result->clips[1].frames.empty());
}

TEST_CASE("serialize_sprite_sheet_clips — non-default fps survives with no clips") {
  using namespace corundum::sprites;
  const auto dir = temp_dir("fps_no_clips");
  const auto path = dir / "sheet.json";

  SpriteSheetClips data;
  data.path = "a.png";
  data.columns = 1;
  data.rows = 1;
  data.frame_width = 8;
  data.frame_height = 8;
  data.anim_fps = 12;

  const auto j = serialize_sprite_sheet_clips(data);
  CHECK(j["animations"]["fps"] == 12);

  write_file(path, j.dump());
  const auto result = load_sprite_sheet_clips(path);
  REQUIRE(result.has_value());
  CHECK(result->anim_fps == 12);
  CHECK(result->clips.empty());
}
