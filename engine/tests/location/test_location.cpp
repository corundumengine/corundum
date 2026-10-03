// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include "temp_dir.hpp"
#include <corundum/location/loader.hpp>
#include <corundum/location/location.hpp>
#include <corundum/location/registry.hpp>
#include <corundum/ui/map.hpp>
#include <corundum/world/flags.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace fs = std::filesystem;
namespace location = corundum::location;
namespace ui = corundum::ui;

namespace {

  void write_file(const fs::path &p, std::string_view content) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p);
    f << content;
  }

  corundum::test::TempDir temp_dir(std::string_view tag) {
    return corundum::test::TempDir{"crpg_test_location_", tag};
  }

} // namespace

TEST_CASE("location loader: parses world and interior destinations") {
  const auto dir = temp_dir("load");
  const auto path = dir / "greyhollow.json";
  write_file(path, R"({
    "schema_version": 1,
    "locations": [
      { "id": "village", "name": "Greyhollow", "return_to_world": true, "col": 40, "row": 32 },
      { "id": "cave", "name": "Cave Mouth", "map": "data/tilemaps/cave.json", "zone": "cave", "col": 3, "row": 9 }
    ]
  })");

  const auto result = location::load_location_file(path);
  REQUIRE(result.has_value());
  REQUIRE(result->size() == 2);
  CHECK((*result)[0].id == "village");
  CHECK((*result)[0].return_to_world);
  CHECK((*result)[0].col == doctest::Approx(40.f));
  CHECK((*result)[1].map == "data/tilemaps/cave.json");
  CHECK((*result)[1].zone == "cave");
}

TEST_CASE("location loader: schema failures are reported") {
  const auto dir = temp_dir("errors");
  write_file(dir / "bad_version.json", R"({"schema_version":2,"locations":[]})");
  write_file(dir / "missing_name.json", R"({"schema_version":1,"locations":[{"id":"x"}]})");
  CHECK_FALSE(location::load_location_file(dir / "bad_version.json").has_value());
  CHECK_FALSE(location::load_location_file(dir / "missing_name.json").has_value());
}

TEST_CASE("location registry: loads a directory and skips duplicate ids") {
  const auto dir = temp_dir("registry");
  write_file(dir / "a.json", R"({
    "schema_version": 1,
    "locations": [ { "id": "a", "name": "A" }, { "id": "shared", "name": "First" } ]
  })");
  write_file(dir / "b.json", R"({
    "schema_version": 1,
    "locations": [ { "id": "b", "name": "B" }, { "id": "shared", "name": "Second" } ]
  })");

  location::Registry registry;
  CHECK(registry.load_all(dir) == 3);
  REQUIRE(registry.find("shared") != nullptr);
  CHECK(registry.find("shared")->name == "First");
}

TEST_CASE("build_map_entries: only discovered locations, marked current by zone") {
  location::Registry registry;
  registry.add(location::Location{.id = "village", .name = "Greyhollow", .return_to_world = true, .zone = "village"});
  registry.add(location::Location{.id = "cave", .map = "cave.json", .name = "Cave Mouth", .zone = "cave"});
  registry.add(location::Location{.id = "mill", .name = "Old Mill", .zone = "mill"});

  corundum::world::FlagStore flags;
  corundum::world::set_flag(flags, location::discovery_flag_key("village"));
  corundum::world::set_flag(flags, location::discovery_flag_key("cave"));

  const std::vector<ui::MapEntry> entries = ui::build_map_entries(registry, flags, "village");
  REQUIRE(entries.size() == 2);
  CHECK(entries[0].name == "Cave Mouth"); // name order
  CHECK_FALSE(entries[0].current);
  CHECK(entries[1].name == "Greyhollow");
  CHECK(entries[1].current);
}
