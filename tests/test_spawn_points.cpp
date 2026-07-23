#include <doctest/doctest.h>

#include <corundum/gameplay/world/actors/actor.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

  void write_file(const fs::path &p, std::string_view content) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p);
    f << content;
  }

  fs::path temp_dir(std::string_view tag) {
    const auto p = fs::temp_directory_path() / "crpg_test_spawn_points" / tag;
    fs::create_directories(p);
    return p;
  }

} // namespace

using corundum::gameplay::world::load_actors;
using corundum::gameplay::world::load_spawn_points;

// ── Missing file ──────────────────────────────────────────────────────────────

TEST_CASE("load_spawn_points — missing file returns empty SpawnPoints") {
  auto result = load_spawn_points("/nonexistent/path/spawn.json");
  REQUIRE(result.has_value());
  CHECK(result->actors.empty());
  CHECK(!result->player.has_value());
}

// ── Actors only (no player block) ──────────────────────────────────────────────

TEST_CASE("load_spawn_points — actors only, no player block") {
  const auto dir = temp_dir("actors_only");
  const auto p = dir / "spawn.json";
  write_file(p, R"({
    "actors": [
      { "col": 2, "row": 3, "sprite": "npc1" },
      { "col": 5, "row": 7, "sprite": "npc2", "dialogue": "greeting", "facing": "east" }
    ]
  })");
  auto result = load_spawn_points(p);
  REQUIRE(result.has_value());
  CHECK(!result->player.has_value());
  REQUIRE(result->actors.size() == 2);
  CHECK(result->actors[0].col == 2);
  CHECK(result->actors[0].sprite_name == "npc1");
  CHECK(result->actors[1].dialogue_ref == "greeting");
  CHECK(result->actors[1].facing == "east");
}

// ── Player block ───────────────────────────────────────────────────────────────

TEST_CASE("load_spawn_points — with player block") {
  const auto dir = temp_dir("with_player");
  const auto p = dir / "spawn.json";
  write_file(p, R"({
    "player": { "col": 10.5, "row": 4.0 },
    "actors": []
  })");
  auto result = load_spawn_points(p);
  REQUIRE(result.has_value());
  REQUIRE(result->player.has_value());
  CHECK(result->player->col == doctest::Approx(10.5f));
  CHECK(result->player->row == doctest::Approx(4.f));
  CHECK(result->actors.empty());
}

TEST_CASE("load_spawn_points — player.col zero is valid") {
  const auto dir = temp_dir("player_col_zero");
  const auto p = dir / "spawn.json";
  write_file(p, R"({
    "player": { "col": 0.0, "row": 0.0 },
    "actors": []
  })");
  auto result = load_spawn_points(p);
  REQUIRE(result.has_value());
  REQUIRE(result->player.has_value());
  CHECK(result->player->col == doctest::Approx(0.f));
  CHECK(result->player->row == doctest::Approx(0.f));
}

TEST_CASE("load_spawn_points — player not an object returns error") {
  const auto dir = temp_dir("player_not_obj");
  const auto p = dir / "spawn.json";
  write_file(p, R"({ "player": 42, "actors": [] })");
  auto result = load_spawn_points(p);
  CHECK(!result.has_value());
}

TEST_CASE("load_spawn_points — player missing col returns error") {
  const auto dir = temp_dir("player_no_col");
  const auto p = dir / "spawn.json";
  write_file(p, R"({ "player": { "row": 5.0 }, "actors": [] })");
  auto result = load_spawn_points(p);
  CHECK(!result.has_value());
}

TEST_CASE("load_spawn_points — player negative col returns error") {
  const auto dir = temp_dir("player_neg_col");
  const auto p = dir / "spawn.json";
  write_file(p, R"({ "player": { "col": -1.0, "row": 5.0 }, "actors": [] })");
  auto result = load_spawn_points(p);
  CHECK(!result.has_value());
}

TEST_CASE("load_spawn_points — malformed JSON returns error") {
  const auto dir = temp_dir("malformed");
  const auto p = dir / "spawn.json";
  write_file(p, "{bad json");
  auto result = load_spawn_points(p);
  CHECK(!result.has_value());
}

TEST_CASE("load_spawn_points — not an object returns error") {
  const auto dir = temp_dir("array");
  const auto p = dir / "spawn.json";
  write_file(p, "[]");
  auto result = load_spawn_points(p);
  CHECK(!result.has_value());
}

// ── load_actors wrapper ────────────────────────────────────────────────────────

TEST_CASE("load_actors — missing file returns empty vector") {
  auto result = load_actors("/nonexistent/path/spawn.json");
  REQUIRE(result.has_value());
  CHECK(result->empty());
}

TEST_CASE("load_actors — returns actors from file with player block") {
  const auto dir = temp_dir("actors_wrapper");
  const auto p = dir / "spawn.json";
  write_file(p, R"({
    "player": { "col": 1.0, "row": 2.0 },
    "actors": [
      { "col": 4, "row": 5, "sprite": "test_npc" }
    ]
  })");
  auto result = load_actors(p);
  REQUIRE(result.has_value());
  REQUIRE(result->size() == 1);
  CHECK((*result)[0].sprite_name == "test_npc");
}
