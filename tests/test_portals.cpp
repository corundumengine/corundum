#include <doctest/doctest.h>

#include <corundum/core/json_io.hpp>
#include <corundum/gameplay/world/portals/portal.hpp>

#include <fstream>

namespace fs = std::filesystem;

namespace {

  void write_file(const fs::path &p, std::string_view content) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p);
    f << content;
  }

  fs::path temp_dir(std::string_view tag) {
    const auto p = fs::temp_directory_path() / "crpg_test_portals" / tag;
    fs::create_directories(p);
    return p;
  }

} // namespace

using corundum::gameplay::world::load_portals;
using corundum::gameplay::world::serialize_portals;

TEST_CASE("load_portals — missing file returns empty vector") {
  auto result = load_portals("/nonexistent/path/portals.json");
  REQUIRE(result.has_value());
  CHECK(result->empty());
}

TEST_CASE("load_portals — malformed JSON returns error containing the path") {
  const auto dir = temp_dir("malformed");
  const auto p = dir / "portals.json";
  write_file(p, "{bad json");
  auto result = load_portals(p);
  REQUIRE(!result.has_value());
  CHECK(result.error().find("Malformed portals") != std::string::npos);
  CHECK(result.error().find(p.string()) != std::string::npos);
}

TEST_CASE("load_portals — valid file round-trip via serialize_portals") {
  const auto dir = temp_dir("roundtrip");
  const auto p = dir / "portals.json";
  write_file(p, R"({
    "portals": [
      { "col": 0, "row": 1, "w": 2, "h": 3, "target_map": "other.json", "spawn_col": 4, "spawn_row": 5 }
    ]
  })");
  auto result = load_portals(p);
  REQUIRE(result.has_value());
  REQUIRE(result->size() == 1);
  const auto portal = (*result)[0];
  CHECK(portal.col == 0.f);
  CHECK(portal.row == 1.f);
  CHECK(portal.w == 2.f);
  CHECK(portal.h == 3.f);
  CHECK(portal.target_map == "other.json");
  CHECK(portal.spawn_col == 4);
  CHECK(portal.spawn_row == 5);

  const auto json = serialize_portals(*result);
  REQUIRE(json.contains("portals"));
  REQUIRE(json["portals"].size() == 1);
  CHECK(json["portals"][0]["col"] == 0);
  CHECK(json["portals"][0]["row"] == 1);
  CHECK(json["portals"][0]["target_map"] == "other.json");
}

TEST_CASE("load_portals — target_chunk_col/row (new keys)") {
  const auto dir = temp_dir("chunk_keys_new");
  const auto p = dir / "portals.json";
  write_file(p, R"({
    "portals": [
      { "col": 0, "row": 0, "w": 1, "h": 1, "spawn_col": 0, "spawn_row": 0,
        "target_chunk_col": 2, "target_chunk_row": 3 }
    ]
  })");
  auto result = load_portals(p);
  REQUIRE(result.has_value());
  REQUIRE(result->size() == 1);
  CHECK((*result)[0].target_chunk_col == 2);
  CHECK((*result)[0].target_chunk_row == 3);
}

TEST_CASE("load_portals — target_chunk_x/y (legacy keys) still accepted") {
  const auto dir = temp_dir("chunk_keys_legacy");
  const auto p = dir / "portals.json";
  write_file(p, R"({
    "portals": [
      { "col": 0, "row": 0, "w": 1, "h": 1, "spawn_col": 0, "spawn_row": 0,
        "target_chunk_x": 5, "target_chunk_y": 6 }
    ]
  })");
  auto result = load_portals(p);
  REQUIRE(result.has_value());
  REQUIRE(result->size() == 1);
  CHECK((*result)[0].target_chunk_col == 5);
  CHECK((*result)[0].target_chunk_row == 6);
}

TEST_CASE("load_portals — serialize writes only new keys") {
  const auto dir = temp_dir("serialize_new_keys");
  const auto p = dir / "portals.json";
  write_file(p, R"({
    "portals": [
      { "col": 0, "row": 0, "w": 1, "h": 1, "target_map": "m.json", "spawn_col": 2, "spawn_row": 3,
        "target_chunk_col": 7, "target_chunk_row": 8 }
    ]
  })");
  auto result = load_portals(p);
  REQUIRE(result.has_value());

  const auto json = serialize_portals(*result);
  const auto &portals = json["portals"];
  REQUIRE(portals.size() == 1);
  CHECK(portals[0].contains("target_chunk_col"));
  CHECK(portals[0].contains("target_chunk_row"));
  CHECK_FALSE(portals[0].contains("target_chunk_x"));
  CHECK_FALSE(portals[0].contains("target_chunk_y"));
  CHECK(portals[0]["target_chunk_col"] == 7);
  CHECK(portals[0]["target_chunk_row"] == 8);
}
