// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include "temp_dir.hpp"

#include <corundum/sprites/character_registry.hpp>
#include <corundum/sprites/sprite.hpp>

#include <cstddef>
#include <filesystem>
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
    return corundum::test::TempDir{"crpg_test_character_registry_", tag};
  }

  /// Minimal valid character sheet: one sprite with authored footprints on a 3x1 grid.
  std::string sheet_json(std::string_view id, std::string_view sprite) {
    return R"({"id":")" + std::string{id} + R"(","path":"game/assets/)" + std::string{id} +
           R"(.png","frame_width":32,"frame_height":32,"frames":{")" + std::string{sprite} +
           R"(":{"col_span":1,"row_span":2,"footprint_col_span":0.25,"footprint_row_span":0.75,)"
           R"("south":[{"col":0,"row":0},{"col":1,"row":0}],"east":[{"col":2,"row":0}]}}})";
  }

} // namespace

TEST_CASE("CharacterRegistry::load_all errors on a missing characters directory") {
  const auto dir = temp_dir("missing");
  corundum::sprites::CharacterRegistry registry;

  const auto result = registry.load_all(dir);
  REQUIRE(!result.has_value());
  CHECK(result.error().find("characters") != std::string::npos);
}

TEST_CASE("CharacterRegistry::load_all errors when no sheets are present") {
  const auto dir = temp_dir("empty");
  fs::create_directories(dir / "characters");
  corundum::sprites::CharacterRegistry registry;

  const auto result = registry.load_all(dir);
  REQUIRE(!result.has_value());
  CHECK(registry.sheets().empty());
}

TEST_CASE("CharacterRegistry::load_all loads a sheet and supports every lookup path") {
  const auto dir = temp_dir("valid");
  write_file(dir / "characters" / "hero.json", sheet_json("heroes", "hero"));

  corundum::sprites::CharacterRegistry registry;
  const auto result = registry.load_all(dir);
  REQUIRE(result.has_value());

  CHECK(registry.sheets().size() == 1);
  CHECK(registry.frames().size() == 1);

  const auto sheet_id = registry.find_sheet("heroes");
  CHECK(sheet_id != corundum::sprites::k_null_sheet);
  const auto *sheet = registry.get_sheet(sheet_id);
  REQUIRE(sheet != nullptr);
  CHECK(sheet->frame_width == 32);
  CHECK(sheet->path == "game/assets/heroes.png");

  const auto *sprite = registry.get_sprite("hero");
  REQUIRE(sprite != nullptr);
  CHECK(sprite->sheet_id == sheet_id);
  CHECK(sprite->row_span == 2);
  CHECK(sprite->anim_frames[static_cast<std::size_t>(corundum::sprites::AnimId::South)].size() == 2);

  const auto sid = registry.get_sprite_id("hero");
  CHECK(sid == sprite->sprite_id);
  CHECK(registry.get_sprite_by_id(sid) == sprite);
}

TEST_CASE("CharacterRegistry lookups return sentinels for unknown names and ids") {
  const auto dir = temp_dir("unknown");
  write_file(dir / "characters" / "hero.json", sheet_json("heroes", "hero"));

  corundum::sprites::CharacterRegistry registry;
  REQUIRE(registry.load_all(dir).has_value());

  CHECK(registry.find_sheet("nope") == corundum::sprites::k_null_sheet);
  CHECK(registry.get_sheet(999) == nullptr);
  CHECK(registry.get_sprite("nope") == nullptr);
  CHECK(registry.get_sprite_id("nope") == corundum::sprites::k_null_sprite_id);
  CHECK(registry.get_sprite_by_id(corundum::sprites::k_null_sprite_id) == nullptr);
  CHECK(registry.get_sprite_by_id(999) == nullptr);
}

TEST_CASE("CharacterRegistry::load_all rejects a duplicate sheet id") {
  const auto dir = temp_dir("dup_sheet");
  write_file(dir / "characters" / "a.json", sheet_json("dup", "hero_a"));
  write_file(dir / "characters" / "b.json", sheet_json("dup", "hero_b"));

  corundum::sprites::CharacterRegistry registry;
  const auto result = registry.load_all(dir);
  REQUIRE(!result.has_value());
  CHECK(result.error().find("Duplicate sheet id") != std::string::npos);

  // The first sheet is retained; the duplicate never registers.
  CHECK(registry.find_sheet("dup") != corundum::sprites::k_null_sheet);
  CHECK(registry.frames().size() == 1);
  CHECK(registry.get_sprite("hero_a") != nullptr);
  CHECK(registry.get_sprite("hero_b") == nullptr);
}

TEST_CASE("CharacterRegistry::load_all rejects a duplicate sprite name without half-registering the sheet") {
  const auto dir = temp_dir("dup_sprite");
  write_file(dir / "characters" / "a.json", sheet_json("sheet_a", "hero"));
  write_file(dir / "characters" / "b.json", sheet_json("sheet_b", "hero"));

  corundum::sprites::CharacterRegistry registry;
  const auto result = registry.load_all(dir);
  REQUIRE(!result.has_value());
  CHECK(result.error().find("Duplicate sprite name") != std::string::npos);

  // The colliding sheet is rejected before any of its state is committed.
  CHECK(registry.find_sheet("sheet_a") != corundum::sprites::k_null_sheet);
  CHECK(registry.find_sheet("sheet_b") == corundum::sprites::k_null_sheet);
  CHECK(registry.frames().size() == 1);
}

TEST_CASE("CharacterRegistry assigns ids in sorted file order regardless of write order") {
  const auto dir = temp_dir("ordered");
  write_file(dir / "characters" / "zulu.json", sheet_json("zulu", "zulu_hero"));
  write_file(dir / "characters" / "alpha.json", sheet_json("alpha", "alpha_hero"));

  corundum::sprites::CharacterRegistry registry;
  REQUIRE(registry.load_all(dir).has_value());

  const auto alpha = registry.find_sheet("alpha");
  const auto zulu = registry.find_sheet("zulu");
  CHECK(alpha < zulu);
  CHECK(registry.get_sprite_id("alpha_hero") < registry.get_sprite_id("zulu_hero"));
}
