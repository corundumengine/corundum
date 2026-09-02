#include <doctest/doctest.h>

#include <corundum/sprites/character_sheet_loader.hpp>
#include <corundum/sprites/character_sheet_serializer.hpp>

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

  const auto path = temp_dir("fps_roundtrip") / "sheet.json";
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