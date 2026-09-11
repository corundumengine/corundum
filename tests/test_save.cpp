#include <doctest/doctest.h>

#include "temp_dir.hpp"

#include <corundum/core/game_config.hpp>
#include <corundum/engine.hpp>
#include <corundum/platform/null/null_platform.hpp>
#include <corundum/quest/quest.hpp>
#include <corundum/quest/registry.hpp>
#include <corundum/quest/status.hpp>
#include <corundum/quest/system.hpp>
#include <corundum/save/save.hpp>
#include <corundum/world/flags.hpp>

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

  // Owns the shared save-tree root; individual cases remove their own tag directory.
  const corundum::test::TempDir g_save_root{"crpg_test_save_", "root"};

  fs::path save_path(std::string_view tag) {
    return g_save_root.path() / std::string{tag} / "save.json";
  }

  void write_save_file(const fs::path &path, const json &j) {
    fs::create_directories(path.parent_path());
    std::ofstream f(path);
    f << j.dump();
  }

  corundum::core::GameConfig make_world_config(const fs::path &fixtures) {
    corundum::core::GameConfig cfg{};
    cfg.window_title = "save_test";
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
    cfg.game_id = "test_game";
    return cfg;
  }

  void adopt_platform(corundum::Engine &engine, unsigned w, unsigned h) {
    corundum::platform::null::NullPlatform platform = corundum::platform::null::make_null_platform(w, h);
    corundum::platform::null::adopt_null_platform(engine, platform);
  }

} // namespace

// ── to_json / from_json ───────────────────────────────────────────────────────

TEST_CASE("save: to_json/from_json round-trips a state with an unknown flag key verbatim") {
  corundum::save::SaveState s;
  s.version = corundum::save::k_save_version;
  s.game_id = "test_game";
  s.mode = "single_map";
  s.map_or_world_id = "interior";
  s.active_zone = "interior";
  s.player_col = 3.5f;
  s.player_row = 7.f;
  s.entered_from_world = true;
  s.flags["quest.test"] = 2;
  s.flags["zone.cave.chest"] = 1;
  s.flags["foo.bar"] = 42; // unknown key — must survive the round trip

  const auto j = corundum::save::to_json(s);
  const auto back = corundum::save::from_json(j);
  REQUIRE(back.has_value());

  CHECK(back->game_id == "test_game");
  CHECK(back->mode == "single_map");
  CHECK(back->map_or_world_id == "interior");
  CHECK(back->active_zone == "interior");
  CHECK(back->player_col == doctest::Approx(3.5f));
  CHECK(back->player_row == doctest::Approx(7.f));
  CHECK(back->entered_from_world);
  CHECK(back->flags.at("quest.test") == 2);
  CHECK(back->flags.at("zone.cave.chest") == 1);
  CHECK(back->flags.at("foo.bar") == 42);
  CHECK(back->flags.size() == 3);
}

TEST_CASE("save: from_json fills defaults for a v1 fixture missing later fields") {
  const json j = {
      {"version", 1},
      {"game_id", "test_game"},
      {"mode", "single_map"},
      {"flags", {{"foo.bar", 7}}},
  };

  const auto state = corundum::save::from_json(j);
  REQUIRE(state.has_value());
  CHECK(state->map_or_world_id.empty());
  CHECK(state->active_zone.empty());
  CHECK(state->player_col == 0.f);
  CHECK(state->player_row == 0.f);
  CHECK_FALSE(state->entered_from_world);
  CHECK(state->flags.at("foo.bar") == 7);
}

TEST_CASE("save: a save with a newer version than the engine supports is refused") {
  const json j = {{"version", 999}, {"game_id", "test_game"}, {"mode", "single_map"}};
  const auto state = corundum::save::from_json(j);
  REQUIRE_FALSE(state.has_value());
  CHECK(state.error().find("newer") != std::string::npos);
}

TEST_CASE("save: a non-object save JSON is refused") {
  const auto state = corundum::save::from_json(json::array({1, 2, 3}));
  REQUIRE_FALSE(state.has_value());
}

// ── load_game guards ──────────────────────────────────────────────────────────

TEST_CASE("save: load_game refuses a save for a different game_id") {
  corundum::Engine engine;
  engine.cfg.game_id = "keystone";

  corundum::save::SaveState s;
  s.game_id = "other_game";
  const fs::path p = save_path("wrong_game");
  write_save_file(p, corundum::save::to_json(s));

  const auto result = corundum::save::load_game(engine, p);
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().find("other_game") != std::string::npos);
  CHECK(engine.flags.empty()); // flags untouched when the game_id guard fails

  fs::remove_all(p.parent_path());
}

TEST_CASE("save: load_game refuses a save whose version is newer than the engine") {
  corundum::Engine engine;
  engine.cfg.game_id = "test_game";

  const fs::path p = save_path("newer_version");
  write_save_file(p, json{{"version", 999}, {"game_id", "test_game"}, {"mode", "single_map"}});

  const auto result = corundum::save::load_game(engine, p);
  REQUIRE_FALSE(result.has_value());

  fs::remove_all(p.parent_path());
}

// ── Integration ───────────────────────────────────────────────────────────────

TEST_CASE("save: single-map save/load restores the interior, zone_id, and player position") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(fs::is_directory(fixtures));

  corundum::core::GameConfig cfg = make_world_config(fixtures);
  cfg.paths.world_manifest_path.clear(); // single-map mode
  cfg.paths.tilemap_path = (fixtures / "tilemaps/interior.json").string();
  REQUIRE(engine.initialize(std::move(cfg)).has_value());
  REQUIRE(engine.render.mode == corundum::render::RenderMode::SingleMap);

  engine.flags["zone.interior.chest"] = 1;
  auto &transforms = engine.scene.world.transforms;
  transforms.pos_col(engine.scene.player) = 5.f;
  transforms.pos_row(engine.scene.player) = 6.f;

  const fs::path p = save_path("integration_single");
  fs::create_directories(p.parent_path());
  REQUIRE(corundum::save::save_game(engine, p).has_value());

  engine.flags.clear();
  transforms.pos_col(engine.scene.player) = 1.f;
  transforms.pos_row(engine.scene.player) = 1.f;

  REQUIRE(corundum::save::load_game(engine, p).has_value());

  CHECK(engine.render.mode == corundum::render::RenderMode::SingleMap);
  CHECK(engine.flags.at("zone.interior.chest") == 1);
  CHECK(engine.scene.zone_id == "interior");

  const auto &restored = engine.scene.world.transforms;
  CHECK(restored.pos_col(engine.scene.player) == doctest::Approx(5.f));
  CHECK(restored.pos_row(engine.scene.player) == doctest::Approx(6.f));

  engine.cleanup();
  fs::remove_all(p.parent_path());
}

TEST_CASE("save: save_game/load_game restore quest lifecycle, zone flags, and player position") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(fs::is_directory(fixtures));

  REQUIRE(engine.initialize(make_world_config(fixtures)).has_value());
  REQUIRE(engine.render.mode == corundum::render::RenderMode::World);

  // Register a quest and advance it to its resolved ending.
  corundum::quest::Quest q;
  q.quest_id = "save_q";
  q.name = "Save Quest";
  q.description = "";
  q.stages.push_back({"start", 1, false, false, {}});
  q.stages.push_back({"complete", 2, true, false, {}});
  engine.quests.add(std::move(q));

  corundum::quest::start(*engine.quests.find("save_q"), engine.flags);
  corundum::quest::advance(*engine.quests.find("save_q"), "complete", engine.flags);
  REQUIRE(corundum::quest::lifecycle(*engine.quests.find("save_q"), engine.flags) ==
          corundum::quest::Lifecycle::Completed);

  // Zone-scoped and NPC state, plus a non-central player position (chunk (1,0)).
  engine.flags["zone.transition.gate"] = 1;
  engine.flags["npc.brann.alive"] = 1;
  auto &transforms = engine.scene.world.transforms;
  transforms.pos_col(engine.scene.player) = 12.f;
  transforms.pos_row(engine.scene.player) = 3.f;

  const fs::path p = save_path("integration");
  fs::create_directories(p.parent_path());
  REQUIRE(corundum::save::save_game(engine, p).has_value());

  // Mutate everything the save captures.
  engine.flags.clear();
  transforms.pos_col(engine.scene.player) = 1.f;
  transforms.pos_row(engine.scene.player) = 1.f;

  REQUIRE(corundum::save::load_game(engine, p).has_value());

  // Quest stage, zone/NPC flags, and player position are restored.
  CHECK(corundum::quest::lifecycle(*engine.quests.find("save_q"), engine.flags) ==
        corundum::quest::Lifecycle::Completed);
  CHECK(engine.flags["zone.transition.gate"] == 1);
  CHECK(engine.flags["npc.brann.alive"] == 1);

  const auto &restored = engine.scene.world.transforms;
  CHECK(restored.pos_col(engine.scene.player) == doctest::Approx(12.f));
  CHECK(restored.pos_row(engine.scene.player) == doctest::Approx(3.f));
  CHECK(engine.scene.zone_id == "transition");
  CHECK_FALSE(engine.entered_from_world);

  engine.cleanup();
  fs::remove_all(p.parent_path());
}