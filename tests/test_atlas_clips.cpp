// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include "temp_dir.hpp"
#include <corundum/sprites/atlas_clips.hpp>

#include <corundum/sprites/atlas_clips_loader.hpp>
#include <corundum/sprites/atlas_clips_serializer.hpp>

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
    return corundum::test::TempDir{"crpg_test_atlas_clips_", tag};
  }

  constexpr std::string_view k_valid_sidecar_json = R"({
    "schema_version": 1,
    "clips": [
      {"name": "water_flow", "fps": 8, "frames": ["Water A_0", "Water A_1", "Water A_2"]}
    ]
  })";

} // namespace

TEST_CASE("load_atlas_clips — valid sidecar parses correctly") {
  const auto dir = temp_dir("valid");
  const auto path = dir / "ground-d.spritedata.json";
  write_file(path, k_valid_sidecar_json);

  const auto result = corundum::sprites::load_atlas_clips(path);
  REQUIRE(result.has_value());
  REQUIRE(result->clips.size() == 1);
  const auto &clip = result->clips[0];
  CHECK(clip.name == "water_flow");
  CHECK(clip.fps == 8);
  REQUIRE(clip.frames.size() == 3);
  CHECK(clip.frames[0] == "Water A_0");
  CHECK(clip.frames[1] == "Water A_1");
  CHECK(clip.frames[2] == "Water A_2");
}

TEST_CASE("load_atlas_clips — non-existent file fails") {
  const auto dir = temp_dir("not_found");
  const auto path = dir / "nonexistent.spritedata.json";
  const auto result = corundum::sprites::load_atlas_clips(path);
  CHECK(!result.has_value());
  CHECK(result.error().find(path.string()) != std::string::npos);
}

TEST_CASE("load_atlas_clips — malformed JSON fails") {
  const auto dir = temp_dir("malformed_json");
  const auto path = dir / "sidecar.json";
  write_file(path, "{ this is not json");

  const auto result = corundum::sprites::load_atlas_clips(path);
  CHECK(!result.has_value());
  CHECK(result.error().find(path.string()) != std::string::npos);
}

TEST_CASE("load_atlas_clips — missing 'schema_version' is treated as legacy version 1") {
  const auto dir = temp_dir("missing_schema_version");
  const auto path = dir / "sidecar.json";
  write_file(path, R"({"clips":[{"name":"a","frames":["x"]}]})");

  const auto result = corundum::sprites::load_atlas_clips(path);
  REQUIRE(result.has_value());
  REQUIRE(result->clips.size() == 1);
  CHECK(result->clips[0].name == "a");
}

TEST_CASE("load_atlas_clips — wrong 'schema_version' fails") {
  const auto dir = temp_dir("wrong_schema_version");
  const auto path = dir / "sidecar.json";
  write_file(path, R"({"schema_version":2,"clips":[]})");

  const auto result = corundum::sprites::load_atlas_clips(path);
  CHECK(!result.has_value());
  CHECK(result.error().find('1') != std::string::npos);
}

TEST_CASE("load_atlas_clips — 'schema_version' wrong type fails") {
  const auto dir = temp_dir("schema_version_wrong_type");
  const auto path = dir / "sidecar.json";
  write_file(path, R"({"schema_version":"1","clips":[]})");

  const auto result = corundum::sprites::load_atlas_clips(path);
  CHECK(!result.has_value());
}

TEST_CASE("load_atlas_clips — 'schema_version' zero fails") {
  const auto dir = temp_dir("schema_version_zero");
  const auto path = dir / "sidecar.json";
  write_file(path, R"({"schema_version":0,"clips":[]})");

  const auto result = corundum::sprites::load_atlas_clips(path);
  CHECK(!result.has_value());
}

TEST_CASE("load_atlas_clips — 'clips' missing is valid (empty)") {
  const auto dir = temp_dir("clips_missing");
  const auto path = dir / "sidecar.json";
  write_file(path, R"({"schema_version":1})");

  const auto result = corundum::sprites::load_atlas_clips(path);
  REQUIRE(result.has_value());
  CHECK(result->clips.empty());
}

TEST_CASE("load_atlas_clips — 'clips' not an array fails") {
  const auto dir = temp_dir("clips_not_array");
  const auto path = dir / "sidecar.json";
  write_file(path, R"({"schema_version":1,"clips":{}})");

  const auto result = corundum::sprites::load_atlas_clips(path);
  CHECK(!result.has_value());
}

TEST_CASE("load_atlas_clips — duplicate clip names fail") {
  const auto dir = temp_dir("duplicate_names");
  const auto path = dir / "sidecar.json";
  write_file(path, R"({"schema_version":1,"clips":[
      {"name":"a","frames":["x"]},
      {"name":"a","frames":["y"]}
  ]})");

  const auto result = corundum::sprites::load_atlas_clips(path);
  CHECK(!result.has_value());
}

TEST_CASE("load_atlas_clips — empty clip name fails") {
  const auto dir = temp_dir("empty_name");
  const auto path = dir / "sidecar.json";
  write_file(path, R"({"schema_version":1,"clips":[{"name":"","frames":["x"]}]})");

  const auto result = corundum::sprites::load_atlas_clips(path);
  CHECK(!result.has_value());
}

TEST_CASE("load_atlas_clips — non-string clip name fails") {
  const auto dir = temp_dir("non_string_name");
  const auto path = dir / "sidecar.json";
  write_file(path, R"({"schema_version":1,"clips":[{"name":1,"frames":["x"]}]})");

  const auto result = corundum::sprites::load_atlas_clips(path);
  CHECK(!result.has_value());
}

TEST_CASE("load_atlas_clips — empty 'frames' array fails") {
  const auto dir = temp_dir("empty_frames");
  const auto path = dir / "sidecar.json";
  write_file(path, R"({"schema_version":1,"clips":[{"name":"a","frames":[]}]})");

  const auto result = corundum::sprites::load_atlas_clips(path);
  CHECK(!result.has_value());
}

TEST_CASE("load_atlas_clips — empty frame name fails") {
  const auto dir = temp_dir("empty_frame_name");
  const auto path = dir / "sidecar.json";
  write_file(path, R"({"schema_version":1,"clips":[{"name":"a","frames":[""]}]})");

  const auto result = corundum::sprites::load_atlas_clips(path);
  CHECK(!result.has_value());
}

TEST_CASE("load_atlas_clips — non-string frame entry fails") {
  const auto dir = temp_dir("non_string_frame");
  const auto path = dir / "sidecar.json";
  write_file(path, R"({"schema_version":1,"clips":[{"name":"a","frames":[1]}]})");

  const auto result = corundum::sprites::load_atlas_clips(path);
  CHECK(!result.has_value());
}

TEST_CASE("load_atlas_clips — 'fps' absent defaults to 8") {
  const auto dir = temp_dir("fps_default");
  const auto path = dir / "sidecar.json";
  write_file(path, R"({"schema_version":1,"clips":[{"name":"a","frames":["x"]}]})");

  const auto result = corundum::sprites::load_atlas_clips(path);
  REQUIRE(result.has_value());
  REQUIRE(result->clips.size() == 1);
  CHECK(result->clips[0].fps == corundum::sprites::k_default_clip_fps);
}

TEST_CASE("load_atlas_clips — non-positive 'fps' fails") {
  const auto dir = temp_dir("fps_nonpositive");
  const auto path = dir / "sidecar.json";
  write_file(path, R"({"schema_version":1,"clips":[{"name":"a","fps":0,"frames":["x"]}]})");

  const auto result = corundum::sprites::load_atlas_clips(path);
  CHECK(!result.has_value());
}

TEST_CASE("load_atlas_clips — non-integer 'fps' fails without throwing") {
  const auto dir = temp_dir("fps_wrong_type");
  const auto path = dir / "sidecar.json";

  for (const std::string_view fps : {R"("fast")", "null", "true", "8.5", "[8]"}) {
    write_file(path, std::format(R"({{"schema_version":1,"clips":[{{"name":"a","fps":{},"frames":["x"]}}]}})", fps));
    const auto result = corundum::sprites::load_atlas_clips(path);
    CHECK_MESSAGE(!result.has_value(), "fps = ", fps);
  }
}

TEST_CASE("load_atlas_clips — out-of-range 'fps' fails") {
  const auto dir = temp_dir("fps_out_of_range");
  const auto path = dir / "sidecar.json";
  // Overflows a 32-bit int if silently narrowed; must be rejected rather than wrapped.
  write_file(path, R"({"schema_version":1,"clips":[{"name":"a","fps":4294967301,"frames":["x"]}]})");

  const auto result = corundum::sprites::load_atlas_clips(path);
  CHECK(!result.has_value());
}

TEST_CASE("serialize_atlas_clips — round-trips through load, including dangling frame names") {
  const auto dir = temp_dir("roundtrip");
  const auto path = dir / "sidecar.json";

  corundum::sprites::AtlasClipsData data;
  data.clips.push_back({.fps = 12, .frames = {"a_0", "a_1", "renamed_or_removed"}, .name = "walk"});
  data.clips.push_back({.fps = 4, .frames = {"b_0"}, .name = "idle"});

  const auto j = corundum::sprites::serialize_atlas_clips(data);
  write_file(path, j.dump());

  const auto result = corundum::sprites::load_atlas_clips(path);
  REQUIRE(result.has_value());
  REQUIRE(result->clips.size() == 2);
  CHECK(result->clips[0].name == "walk");
  CHECK(result->clips[0].fps == 12);
  REQUIRE(result->clips[0].frames.size() == 3);
  CHECK(result->clips[0].frames[2] == "renamed_or_removed");
  CHECK(result->clips[1].name == "idle");
  CHECK(result->clips[1].fps == 4);
}

TEST_CASE("serialize_atlas_clips — writes schema_version and omits default 'fps'") {
  corundum::sprites::AtlasClipsData data;
  data.clips.push_back({.fps = corundum::sprites::k_default_clip_fps, .frames = {"a_0"}, .name = "idle"});

  const auto j = corundum::sprites::serialize_atlas_clips(data);
  CHECK(j["schema_version"] == corundum::sprites::k_atlas_clips_schema_version);
  REQUIRE(j["clips"].size() == 1);
  CHECK(!j["clips"][0].contains("fps"));

  const auto dir = temp_dir("default_fps");
  const auto path = dir / "sidecar.json";
  write_file(path, j.dump());

  const auto result = corundum::sprites::load_atlas_clips(path);
  REQUIRE(result.has_value());
  REQUIRE(result->clips.size() == 1);
  CHECK(result->clips[0].fps == corundum::sprites::k_default_clip_fps);
}

TEST_CASE("serialize_atlas_clips — empty data round-trips to empty clips") {
  const auto dir = temp_dir("empty");
  const auto path = dir / "sidecar.json";
  write_file(path, corundum::sprites::serialize_atlas_clips({}).dump());

  const auto result = corundum::sprites::load_atlas_clips(path);
  REQUIRE(result.has_value());
  CHECK(result->clips.empty());
}

TEST_CASE("atlas_clips_sidecar_path — replaces the extension with .spritedata.json") {
  const auto p = corundum::sprites::atlas_clips_sidecar_path("data/sprite_sheets/environments/ground-d.json");
  CHECK(p == fs::path("data/sprite_sheets/environments/ground-d.spritedata.json"));
}
