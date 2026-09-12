// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include "temp_dir.hpp"

#include <corundum/core/game_config.hpp>
#include <corundum/engine.hpp>
#include <corundum/entities/world.hpp>
#include <corundum/platform/null/null_platform.hpp>
#include <corundum/world/actors/actor.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

  void write_file(const fs::path &p, std::string_view content) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p);
    f << content;
  }

  corundum::test::TempDir temp_dir(std::string_view tag) {
    return corundum::test::TempDir{"crpg_test_spawn_points_", tag};
  }

  corundum::core::GameConfig make_world_config(const fs::path &fixtures) {
    corundum::core::GameConfig cfg{};
    cfg.window_title = "spawn_points_test";
    cfg.win_w = 320.f;
    cfg.win_h = 240.f;
    cfg.paths.sprites_dir = (fixtures / "sprites").string();
    cfg.paths.font_dir = (fixtures / "fonts").string();
    cfg.paths.game_font = "missing.ttf"; // NullRenderer ignores file existence
    cfg.paths.world_manifest_path = (fixtures / "worlds/transition/manifest.json").string();
    cfg.paths.spawn_points_dir = (fixtures / "spawn_points").string();
    cfg.paths.portals_dir = (fixtures / "portals").string();
    cfg.paths.dialogue_dir.clear();
    cfg.paths.quests_dir.clear();
    cfg.paths.sounds_dir.clear();
    return cfg;
  }

} // namespace

using corundum::world::load_actors;
using corundum::world::load_spawn_points;

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

// ── Actor id parsing ───────────────────────────────────────────────────────────

TEST_CASE("load_spawn_points — parses the actor id field") {
  const auto dir = temp_dir("actor_id");
  const auto p = dir / "spawn.json";
  write_file(p, R"({
    "actors": [
      { "col": 2, "row": 3, "sprite": "npc1", "id": "brann" },
      { "col": 5, "row": 7, "sprite": "npc2" }
    ]
  })");
  auto result = load_spawn_points(p);
  REQUIRE(result.has_value());
  REQUIRE(result->actors.size() == 2);
  CHECK(result->actors[0].id == "brann");
  CHECK(result->actors[1].id.empty());
}

// ── find_actor wiring ─────────────────────────────────────────────────────────

TEST_CASE("find_actor — world boot populates actor_ids from spawn points") {
  corundum::Engine engine{};
  auto platform = corundum::platform::null::make_null_platform(320, 240);
  corundum::platform::null::adopt_null_platform(engine, platform);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(fs::is_directory(fixtures));
  REQUIRE(engine.initialize(make_world_config(fixtures)).has_value());

  // a00 (chunk 0,0) and a11 (chunk 1,1) carry ids in the fixture spawn points.
  const auto a00 = corundum::entities::find_actor(engine.scene.world, "a00");
  REQUIRE(a00.has_value());
  CHECK(engine.scene.world.entities.is_live(*a00));

  const auto a11 = corundum::entities::find_actor(engine.scene.world, "a11");
  REQUIRE(a11.has_value());
  CHECK_FALSE(*a11 == *a00);

  // The player entity and unknown ids have no row.
  CHECK_FALSE(corundum::entities::find_actor(engine.scene.world, "player").has_value());
  CHECK_FALSE(corundum::entities::find_actor(engine.scene.world, "missing").has_value());

  // Despawning the actor drops its id row.
  corundum::entities::despawn(engine.scene.world, *a00);
  CHECK_FALSE(corundum::entities::find_actor(engine.scene.world, "a00").has_value());

  engine.cleanup();
}
