// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <doctest/doctest.h>

#include "temp_dir.hpp"
#include <corundum/sprites/sprite.hpp>
#include <nlohmann/json_fwd.hpp>

#include <corundum/sprites/character_sheet_loader.hpp>
#include <corundum/sprites/character_sheet_serializer.hpp>

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
    return corundum::test::TempDir{"crpg_test_char_sheet_serializer_", tag};
  }

} // namespace

TEST_CASE("character sheet fps survives a serialize -> load round trip") {
  using namespace corundum::sprites;
  CharacterSheetData data;
  data.id = "hero_sheet";
  data.path = "hero.png";
  data.frame_width = 32;
  data.frame_height = 32;

  CharacterSpriteEntry walk;
  walk.name = "player_walk";
  walk.fps = 12.f;
  walk.anim_frames[static_cast<std::size_t>(AnimId::South)] = {{0, 0}, {1, 0}};
  data.sprites.push_back(walk);

  CharacterSpriteEntry idle;
  idle.name = "player_idle"; // fps left at 0 -> must not appear in JSON
  idle.anim_frames[static_cast<std::size_t>(AnimId::South)] = {{0, 1}};
  data.sprites.push_back(idle);

  const nlohmann::json j = serialize_character_sheet(data);
  CHECK(j.at("frames").at("player_walk").at("fps").get<float>() == doctest::Approx(12.f));
  CHECK_FALSE(j.at("frames").at("player_idle").contains("fps"));

  const auto dir = temp_dir("fps_roundtrip");
  const auto path = dir / "sheet.json";
  write_file(path, j.dump());
  const auto loaded = load_character_sheet(path);
  REQUIRE(loaded.has_value());

  const CharacterSpriteEntry *lw = nullptr;
  const CharacterSpriteEntry *li = nullptr;
  for (const auto &s : loaded->sprites) {
    if (s.name == "player_walk")
      lw = &s;
    if (s.name == "player_idle")
      li = &s;
  }
  REQUIRE(lw != nullptr);
  REQUIRE(li != nullptr);
  CHECK(lw->fps == doctest::Approx(12.f));
  CHECK(li->fps == doctest::Approx(0.f));
}

TEST_CASE("character sheet footprint authored state survives a serialize -> load round trip") {
  using namespace corundum::sprites;
  const auto dir = temp_dir("footprint_roundtrip");
  const auto path = dir / "sheet.json";
  // One sprite authors a footprint, the other relies on the loader's defaults.
  write_file(
      path,
      R"({"id":"sheet","path":"s.png","frame_width":16,"frame_height":16,"frames":{"authored":{"col_span":1,"row_span":1,"footprint_col_span":0.5,"footprint_row_span":0.9,"south":[{"col":0,"row":0}]},"defaulted":{"col_span":1,"row_span":1,"south":[{"col":0,"row":0}]}}})");

  const auto loaded = load_character_sheet(path);
  REQUIRE(loaded.has_value());

  const nlohmann::json j = serialize_character_sheet(*loaded);
  CHECK(j.at("frames").at("authored").contains("footprint_col_span"));
  CHECK_FALSE(j.at("frames").at("defaulted").contains("footprint_col_span"));

  const auto reload_path = dir / "reloaded.json";
  write_file(reload_path, j.dump());
  const auto reloaded = load_character_sheet(reload_path);
  REQUIRE(reloaded.has_value());
  REQUIRE(reloaded->sprites.size() == 2);

  for (const auto &s : reloaded->sprites) {
    if (s.name == "authored") {
      CHECK(s.footprint_authored); // the keys were authored, so the registry stays quiet
      CHECK(s.footprint_col_span == doctest::Approx(0.5f));
      CHECK(s.footprint_row_span == doctest::Approx(0.9f));
    } else if (s.name == "defaulted") {
      CHECK_FALSE(s.footprint_authored); // omission must not fabricate an authored footprint
      CHECK(s.footprint_col_span == doctest::Approx(k_default_footprint_col_span));
      CHECK(s.footprint_row_span == doctest::Approx(k_default_footprint_row_span));
    }
  }
}

TEST_CASE("character sheet walk_around_offset is omitted at its default and round-trips otherwise") {
  using namespace corundum::sprites;
  CharacterSheetData data;
  data.id = "sheet";
  data.path = "s.png";
  data.frame_width = 16;
  data.frame_height = 16;

  CharacterSpriteEntry custom;
  custom.name = "custom";
  custom.walk_around_offset = 0.9f;
  custom.anim_frames[static_cast<std::size_t>(AnimId::South)] = {{.col = 0, .row = 0}};
  data.sprites.push_back(custom);

  CharacterSpriteEntry defaulted;
  defaulted.name = "defaulted";
  defaulted.anim_frames[static_cast<std::size_t>(AnimId::South)] = {{.col = 0, .row = 1}};
  data.sprites.push_back(defaulted);

  const nlohmann::json j = serialize_character_sheet(data);
  CHECK(j.at("frames").at("custom").at("walk_around_offset").get<float>() == doctest::Approx(0.9f));
  CHECK_FALSE(j.at("frames").at("defaulted").contains("walk_around_offset"));

  const auto dir = temp_dir("walk_around_roundtrip");
  const auto path = dir / "sheet.json";
  write_file(path, j.dump());
  const auto loaded = load_character_sheet(path);
  REQUIRE(loaded.has_value());
  for (const auto &s : loaded->sprites) {
    if (s.name == "custom")
      CHECK(s.walk_around_offset == doctest::Approx(0.9f));
    if (s.name == "defaulted")
      CHECK(s.walk_around_offset == doctest::Approx(k_default_walk_around_offset));
  }
}

TEST_CASE("load_character_sheet — fps defaults to 0.f when absent") {
  const auto dir = temp_dir("fps_default");
  const auto path = dir / "sheet.json";
  write_file(
      path,
      R"({"id":"x","path":"p.png","frame_width":16,"frame_height":16,"frames":{"s":{"col_span":1,"row_span":1,"south":[{"col":0,"row":0}]}}})");

  auto result = corundum::sprites::load_character_sheet(path);
  REQUIRE(result.has_value());
  CHECK(result->sprites[0].fps == doctest::Approx(0.f));
}