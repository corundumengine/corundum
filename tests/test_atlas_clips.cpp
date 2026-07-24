#include <doctest/doctest.h>

#include <corundum/resources/atlas_clips_loader.hpp>
#include <corundum/resources/atlas_clips_serializer.hpp>

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
    const auto p = fs::temp_directory_path() / "crpg_test_atlas_clips" / tag;
    fs::create_directories(p);
    return p;
  }

  constexpr std::string_view VALID_SIDECAR_JSON = R"({
    "schema_version": 1,
    "atlas": "ground-d.json",
    "clips": [
      {"name": "water_flow", "fps": 8, "frames": ["Water A_0", "Water A_1", "Water A_2"]}
    ]
  })";

} // namespace

TEST_CASE("load_atlas_clips — valid sidecar parses correctly") {
  const auto dir = temp_dir("valid");
  const auto path = dir / "ground-d.spritedata.json";
  write_file(path, VALID_SIDECAR_JSON);

  auto result = corundum::resources::load_atlas_clips(path);
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
  auto result = corundum::resources::load_atlas_clips(path);
  CHECK(!result.has_value());
  CHECK(result.error().find(path.string()) != std::string::npos);
}

TEST_CASE("load_atlas_clips — missing 'schema_version' fails") {
  const auto dir = temp_dir("missing_schema_version");
  const auto path = dir / "sidecar.json";
  write_file(path, R"({"clips":[]})");

  auto result = corundum::resources::load_atlas_clips(path);
  CHECK(!result.has_value());
}

TEST_CASE("load_atlas_clips — wrong 'schema_version' fails") {
  const auto dir = temp_dir("wrong_schema_version");
  const auto path = dir / "sidecar.json";
  write_file(path, R"({"schema_version":2,"clips":[]})");

  auto result = corundum::resources::load_atlas_clips(path);
  CHECK(!result.has_value());
  CHECK(result.error().find("1") != std::string::npos);
}

TEST_CASE("load_atlas_clips — 'clips' missing is valid (empty)") {
  const auto dir = temp_dir("clips_missing");
  const auto path = dir / "sidecar.json";
  write_file(path, R"({"schema_version":1})");

  auto result = corundum::resources::load_atlas_clips(path);
  REQUIRE(result.has_value());
  CHECK(result->clips.empty());
}

TEST_CASE("load_atlas_clips — 'clips' not an array fails") {
  const auto dir = temp_dir("clips_not_array");
  const auto path = dir / "sidecar.json";
  write_file(path, R"({"schema_version":1,"clips":{}})");

  auto result = corundum::resources::load_atlas_clips(path);
  CHECK(!result.has_value());
}

TEST_CASE("load_atlas_clips — duplicate clip names fail") {
  const auto dir = temp_dir("duplicate_names");
  const auto path = dir / "sidecar.json";
  write_file(path, R"({"schema_version":1,"clips":[
      {"name":"a","frames":["x"]},
      {"name":"a","frames":["y"]}
  ]})");

  auto result = corundum::resources::load_atlas_clips(path);
  CHECK(!result.has_value());
}

TEST_CASE("load_atlas_clips — empty clip name fails") {
  const auto dir = temp_dir("empty_name");
  const auto path = dir / "sidecar.json";
  write_file(path, R"({"schema_version":1,"clips":[{"name":"","frames":["x"]}]})");

  auto result = corundum::resources::load_atlas_clips(path);
  CHECK(!result.has_value());
}

TEST_CASE("load_atlas_clips — non-string frame entry fails") {
  const auto dir = temp_dir("non_string_frame");
  const auto path = dir / "sidecar.json";
  write_file(path, R"({"schema_version":1,"clips":[{"name":"a","frames":[1]}]})");

  auto result = corundum::resources::load_atlas_clips(path);
  CHECK(!result.has_value());
}

TEST_CASE("load_atlas_clips — 'fps' absent defaults to 8") {
  const auto dir = temp_dir("fps_default");
  const auto path = dir / "sidecar.json";
  write_file(path, R"({"schema_version":1,"clips":[{"name":"a","frames":["x"]}]})");

  auto result = corundum::resources::load_atlas_clips(path);
  REQUIRE(result.has_value());
  REQUIRE(result->clips.size() == 1);
  CHECK(result->clips[0].fps == 8);
}

TEST_CASE("load_atlas_clips — non-positive 'fps' fails") {
  const auto dir = temp_dir("fps_nonpositive");
  const auto path = dir / "sidecar.json";
  write_file(path, R"({"schema_version":1,"clips":[{"name":"a","fps":0,"frames":["x"]}]})");

  auto result = corundum::resources::load_atlas_clips(path);
  CHECK(!result.has_value());
}

TEST_CASE("serialize_atlas_clips — round-trips through load, including dangling frame names") {
  const auto dir = temp_dir("roundtrip");
  const auto path = dir / "sidecar.json";

  corundum::resources::AtlasClipsData data;
  data.clips.push_back({"walk", 12, {"a_0", "a_1", "renamed_or_removed"}});
  data.clips.push_back({"idle", 4, {"b_0"}});

  const auto j = corundum::resources::serialize_atlas_clips(data);
  write_file(path, j.dump());

  auto result = corundum::resources::load_atlas_clips(path);
  REQUIRE(result.has_value());
  REQUIRE(result->clips.size() == 2);
  CHECK(result->clips[0].name == "walk");
  CHECK(result->clips[0].fps == 12);
  REQUIRE(result->clips[0].frames.size() == 3);
  CHECK(result->clips[0].frames[2] == "renamed_or_removed");
  CHECK(result->clips[1].name == "idle");
  CHECK(result->clips[1].fps == 4);
}

TEST_CASE("atlas_clips_sidecar_path — replaces the extension with .spritedata.json") {
  const auto p = corundum::resources::atlas_clips_sidecar_path("data/sprite_sheets/environments/ground-d.json");
  CHECK(p == fs::path("data/sprite_sheets/environments/ground-d.spritedata.json"));
}
