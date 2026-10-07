// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include "font_fixtures.hpp"
#include "temp_dir.hpp"
#include <corundum/render/render_state.hpp>

// json_fwd.hpp is include-cleaner's provider for nlohmann::json; json.hpp is still
// required to construct json values (the forward header is incomplete).
#include <nlohmann/json.hpp> // NOLINT(misc-include-cleaner): constructors live in json.hpp
#include <nlohmann/json_fwd.hpp>

#include <corundum/core/environment.hpp>
#include <corundum/core/game_config.hpp>
#include <corundum/core/json_io.hpp>
#include <corundum/engine.hpp>
#include <corundum/gameplay/gameplay.hpp>
#include <corundum/gameplay/quest/quest.hpp>
#include <corundum/gameplay/quest/registry.hpp>
#include <corundum/gameplay/quest/status.hpp>
#include <corundum/gameplay/quest/system.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/platform/null/null_platform.hpp>
#include <corundum/save/save.hpp>
#include <corundum/world/flags.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

  // Owns the shared save-tree root; individual cases remove their own tag directory.
  // Held behind a function-local static: TempDir's constructor touches the filesystem
  // and can throw, which must not happen during static initialization before main.
  const corundum::test::TempDir &save_root() {
    static const corundum::test::TempDir root{"crpg_test_save_", "root"};
    return root;
  }

  fs::path save_path(std::string_view tag) {
    return save_root().path() / std::string{tag} / "save.json";
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
    cfg.show_title = false;
    cfg.paths.sprites_dir = (fixtures / "sprites").string();
    cfg.paths.font_dir = (fixtures / "fonts").string();
    corundum::test::set_missing_fonts(cfg.paths);
    cfg.paths.world_manifest_path = (fixtures / "worlds/transition/manifest.json").string();
    cfg.paths.spawn_points_dir = (fixtures / "spawn_points").string();
    cfg.paths.portals_dir = (fixtures / "portals").string();
    cfg.paths.dialogue_dir.clear();
    cfg.paths.quests_dir.clear();
    cfg.paths.sounds_dir.clear();
    cfg.game_id = "test_game";
    return cfg;
  }

  // Saves the current value of an environment variable, installs the requested state for the
  // guard's lifetime, and restores the original on destruction. Duplicated from
  // engine/tests/core/test_user_data_dir.cpp so the save tests can point user_data_dir at a
  // scratch tree without leaking into later cases.
  struct EnvGuard {
    std::string name;
    std::optional<std::string> saved;

    EnvGuard(std::string_view variable_name, const std::string &value)
        : name(variable_name), saved(corundum::core::read_env(name.c_str())) {
      set_env(name, value);
    }

    ~EnvGuard() {
      if (saved)
        set_env(name, *saved);
      else
        unset_env(name);
    }

    EnvGuard(const EnvGuard &) = delete;
    EnvGuard(EnvGuard &&) = delete;
    EnvGuard &operator=(const EnvGuard &) = delete;
    EnvGuard &operator=(EnvGuard &&) = delete;

  private:
    // setenv/unsetenv are POSIX, declared by the platform's <stdlib.h>.
    // include-cleaner's only accepted header for them is Darwin's private
    // <_stdlib.h>, which is not portable.
    // NOLINTBEGIN(misc-include-cleaner)
    static void set_env(const std::string &variable_name, const std::string &value) {
#ifdef _WIN32
      _putenv_s(variable_name.c_str(), value.c_str());
#else
      setenv(variable_name.c_str(), value.c_str(), 1);
#endif
    }

    static void unset_env(const std::string &variable_name) {
#ifdef _WIN32
      _putenv_s(variable_name.c_str(), "");
#else
      unsetenv(variable_name.c_str());
#endif
    }

    // NOLINTEND(misc-include-cleaner)
  };

  // Points user_data_dir at a scratch tree for its lifetime: HOME, XDG_DATA_HOME and (on
  // Windows) APPDATA all resolve into @c home, so saves_directory() lands under it.
  struct ScratchUserData {
    corundum::test::TempDir home{"crpg_test_save_", "user_data"};
    EnvGuard home_guard{"HOME", home.path().string()};
    EnvGuard xdg_guard{"XDG_DATA_HOME", home.path().string()};
#ifdef _WIN32
    EnvGuard appdata_guard{"APPDATA", home.path().string()};
#endif
  };

  void adopt_platform(corundum::Engine &engine, unsigned w, unsigned h) {
    corundum::platform::null::NullPlatform platform = corundum::platform::null::make_null_platform(w, h);
    corundum::platform::null::adopt_null_platform(engine, platform);
  }

} // namespace

// ── serialize / parse ─────────────────────────────────────────────────────────

TEST_CASE("save: serialize/parse round-trips a state with an unknown flag key verbatim") {
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

  const auto j = corundum::save::serialize(s);
  const auto back = corundum::save::parse(j);
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

TEST_CASE("save: parse fills defaults for a v1 fixture missing later fields") {
  const json j = {
      {"version", 1},
      {"game_id", "test_game"},
      {"mode", "single_map"},
      {"flags", {{"foo.bar", 7}}},
  };

  const auto state = corundum::save::parse(j);
  REQUIRE(state.has_value());
  CHECK(state->map_or_world_id.empty());
  CHECK(state->active_zone.empty());
  CHECK(state->player_col == 0.f);
  CHECK(state->player_row == 0.f);
  CHECK_FALSE(state->entered_from_world);
  CHECK(state->flags.at("foo.bar") == 7);
}

TEST_CASE("save: a save with a different version than the engine supports is refused") {
  const json newer = {{"version", 999}, {"game_id", "test_game"}, {"mode", "single_map"}};
  const auto from_newer = corundum::save::parse(newer);
  REQUIRE_FALSE(from_newer.has_value());
  CHECK(from_newer.error().find("version") != std::string::npos);

  // There is no migration path: an older version is refused just as a newer one is.
  const json older = {{"version", 0}, {"game_id", "test_game"}, {"mode", "single_map"}};
  CHECK_FALSE(corundum::save::parse(older).has_value());
}

TEST_CASE("save: a non-object save JSON is refused") {
  const auto state = corundum::save::parse(json::array({1, 2, 3}));
  REQUIRE_FALSE(state.has_value());
}

TEST_CASE("save: parse rejects a field with the wrong JSON type") {
  const json j = {
      {"version", 1},
      {"game_id", 42}, // must be a string
      {"mode", "single_map"},
  };

  const auto state = corundum::save::parse(j);
  REQUIRE_FALSE(state.has_value());
  CHECK(state.error().find("game_id") != std::string::npos);
}

TEST_CASE("save: parse rejects a non-numeric player position") {
  const json j = {
      {"version", 1},
      {"mode", "single_map"},
      {"player_col", "five"},
  };

  const auto state = corundum::save::parse(j);
  REQUIRE_FALSE(state.has_value());
  CHECK(state.error().find("player_col") != std::string::npos);
}

TEST_CASE("save: parse rejects a non-integer version") {
  const json j = {{"version", "one"}, {"mode", "single_map"}};

  const auto state = corundum::save::parse(j);
  REQUIRE_FALSE(state.has_value());
  CHECK(state.error().find("version") != std::string::npos);
}

TEST_CASE("save: parse rejects an unknown mode") {
  const json j = {{"version", 1}, {"mode", "overworld"}};

  const auto state = corundum::save::parse(j);
  REQUIRE_FALSE(state.has_value());
  CHECK(state.error().find("mode") != std::string::npos);
}

// ── load_game guards ──────────────────────────────────────────────────────────

TEST_CASE("save: load_game refuses a save for a different game_id") {
  corundum::Engine engine;
  engine.cfg.game_id = "keystone";

  corundum::save::SaveState s;
  s.game_id = "other_game";
  const fs::path p = save_path("wrong_game");
  write_save_file(p, corundum::save::serialize(s));

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
  REQUIRE(corundum::save::save_game(engine, p, "test", 0).has_value());

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
  corundum::gameplay::Gameplay gameplay{engine};
  REQUIRE(engine.render.mode == corundum::render::RenderMode::World);

  // Register a quest and advance it to its resolved ending.
  corundum::gameplay::quest::Quest q;
  q.quest_id = "save_q";
  q.name = "Save Quest";
  q.description = "";
  q.stages.push_back({.name = "start", .sequence = 1});
  q.stages.push_back({.name = "complete", .resolved = true, .sequence = 2});
  gameplay.quests.add(std::move(q));

  corundum::gameplay::quest::start(*gameplay.quests.find("save_q"), engine.flags);
  corundum::gameplay::quest::advance(*gameplay.quests.find("save_q"), "complete", engine.flags);
  REQUIRE(corundum::gameplay::quest::lifecycle(*gameplay.quests.find("save_q"), engine.flags) ==
          corundum::gameplay::quest::Lifecycle::Completed);

  // Zone-scoped and NPC state, plus a non-central player position (chunk (1,0)).
  engine.flags["zone.transition.gate"] = 1;
  engine.flags["npc.brann.alive"] = 1;
  auto &transforms = engine.scene.world.transforms;
  transforms.pos_col(engine.scene.player) = 12.f;
  transforms.pos_row(engine.scene.player) = 3.f;

  const fs::path p = save_path("integration");
  fs::create_directories(p.parent_path());
  REQUIRE(corundum::save::save_game(engine, p, "test", 0).has_value());

  // Mutate everything the save captures.
  engine.flags.clear();
  transforms.pos_col(engine.scene.player) = 1.f;
  transforms.pos_row(engine.scene.player) = 1.f;

  REQUIRE(corundum::save::load_game(engine, p).has_value());

  // Quest stage, zone/NPC flags, and player position are restored.
  CHECK(corundum::gameplay::quest::lifecycle(*gameplay.quests.find("save_q"), engine.flags) ==
        corundum::gameplay::quest::Lifecycle::Completed);
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

TEST_CASE("save: world save/load preserves a fractional player position") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(fs::is_directory(fixtures));
  REQUIRE(engine.initialize(make_world_config(fixtures)).has_value());
  // NOLINTNEXTLINE(misc-const-correctness)
  corundum::gameplay::Gameplay gameplay{engine};
  REQUIRE(engine.render.mode == corundum::render::RenderMode::World);

  // World-mode saves keep the player's sub-tile position: the spawn pipeline must not
  // truncate it back to an integer tile.
  auto &transforms = engine.scene.world.transforms;
  transforms.pos_col(engine.scene.player) = 12.25f;
  transforms.pos_row(engine.scene.player) = 3.75f;

  const fs::path p = save_path("fractional_world");
  fs::create_directories(p.parent_path());
  REQUIRE(corundum::save::save_game(engine, p, "test", 0).has_value());

  transforms.pos_col(engine.scene.player) = 1.f;
  transforms.pos_row(engine.scene.player) = 1.f;

  REQUIRE(corundum::save::load_game(engine, p).has_value());

  const auto &restored = engine.scene.world.transforms;
  CHECK(restored.pos_col(engine.scene.player) == doctest::Approx(12.25f));
  CHECK(restored.pos_row(engine.scene.player) == doctest::Approx(3.75f));

  engine.cleanup();
  fs::remove_all(p.parent_path());
}

TEST_CASE("save: load_game refuses a save whose world manifest differs") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(fs::is_directory(fixtures));
  REQUIRE(engine.initialize(make_world_config(fixtures)).has_value());
  // NOLINTNEXTLINE(misc-const-correctness)
  corundum::gameplay::Gameplay gameplay{engine};
  REQUIRE(engine.render.mode == corundum::render::RenderMode::World);

  corundum::save::SaveState s;
  s.game_id = "test_game";
  s.mode = "world";
  s.map_or_world_id = "some/other/world/manifest.json";

  const fs::path p = save_path("wrong_world");
  write_save_file(p, corundum::save::serialize(s));

  const auto result = corundum::save::load_game(engine, p);
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().find("world") != std::string::npos);

  engine.cleanup();
  fs::remove_all(p.parent_path());
}

TEST_CASE("save: a failed load leaves the engine's flags untouched") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(fs::is_directory(fixtures));

  corundum::core::GameConfig cfg = make_world_config(fixtures);
  cfg.paths.world_manifest_path.clear(); // single-map mode
  cfg.paths.tilemap_path = (fixtures / "tilemaps/interior.json").string();
  REQUIRE(engine.initialize(std::move(cfg)).has_value());

  engine.flags["keep.me"] = 1;

  corundum::save::SaveState s;
  s.game_id = "test_game";
  s.mode = "single_map";
  s.map_or_world_id = (fixtures / "tilemaps/does_not_exist.json").string();

  const fs::path p = save_path("failed_load");
  write_save_file(p, corundum::save::serialize(s));

  const auto result = corundum::save::load_game(engine, p);
  REQUIRE_FALSE(result.has_value());
  CHECK(engine.flags.count("keep.me") == 1);
  CHECK(engine.flags.size() == 1);

  engine.cleanup();
  fs::remove_all(p.parent_path());
}

// ── SaveMeta, slots and the saves directory ───────────────────────────────────

TEST_CASE("save: SaveMeta round-trips through serialize/parse") {
  corundum::save::SaveState s;
  s.game_id = "test_game";
  s.mode = "single_map";
  s.map_or_world_id = "interior";
  s.meta.location_name = "Greyhollow";
  s.meta.playtime_seconds = 3725;
  s.meta.saved_at_unix = 1'700'000'000;

  const auto back = corundum::save::parse(corundum::save::serialize(s));
  REQUIRE(back.has_value());
  CHECK(back->meta.location_name == "Greyhollow");
  CHECK(back->meta.playtime_seconds == 3725);
  CHECK(back->meta.saved_at_unix == 1'700'000'000);
}

TEST_CASE("save: parse defaults the meta block when it is absent") {
  const json j = {{"version", 1}, {"game_id", "test_game"}, {"mode", "single_map"}};
  const auto state = corundum::save::parse(j);
  REQUIRE(state.has_value());
  CHECK(state->meta.location_name.empty());
  CHECK(state->meta.playtime_seconds == 0);
  CHECK(state->meta.saved_at_unix == 0);
}

TEST_CASE("save: manual_slot_id formats zero-padded slot ids") {
  CHECK(corundum::save::k_manual_slot_count == 10);
  CHECK(corundum::save::manual_slot_id(0) == "slot_01");
  CHECK(corundum::save::manual_slot_id(9) == "slot_10");
}

TEST_CASE("save: saves_directory fails on an empty game_id") {
  corundum::core::GameConfig cfg{};
  cfg.game_id.clear();
  const auto result = corundum::save::saves_directory(cfg);
  REQUIRE_FALSE(result.has_value());
}

TEST_CASE("save: save_game creates a missing parent directory and leaves no temp file") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(fs::is_directory(fixtures));
  REQUIRE(engine.initialize(make_world_config(fixtures)).has_value());

  const fs::path p = save_root().path() / "creates_parent" / "nested" / "save.json";
  REQUIRE_FALSE(fs::exists(p.parent_path()));

  const auto result = corundum::save::save_game(engine, p, "Village", 90);
  REQUIRE(result.has_value());
  CHECK(fs::exists(p));

  fs::path temp = p;
  temp += ".tmp";
  CHECK_FALSE(fs::exists(temp));

  const auto read = corundum::core::read_json(p);
  REQUIRE(read.has_value());
  const auto state = corundum::save::parse(*read);
  REQUIRE(state.has_value());
  CHECK(state->meta.location_name == "Village");
  CHECK(state->meta.playtime_seconds == 90);
  CHECK(state->meta.saved_at_unix > 0);

  engine.cleanup();
  fs::remove_all(p.parent_path().parent_path());
}

// doctest's REQUIRE/CHECK macros expand to control flow, so the assertion count — not the test's
// logic — dominates this metric.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("save: list_saves returns valid rows and marks corrupt ones") {
  const corundum::test::TempDir dir{"crpg_test_save_", "list"};
  const fs::path &root = dir.path();

  const auto write_valid = [&](const fs::path &path, const std::string &game_id, const std::string &location) {
    corundum::save::SaveState s;
    s.game_id = game_id;
    s.mode = "single_map";
    s.map_or_world_id = "interior";
    s.meta.location_name = location;
    s.meta.playtime_seconds = 42;
    s.meta.saved_at_unix = 1'700'000'000;
    write_save_file(path, corundum::save::serialize(s));
  };

  write_valid(root / "slot_01.json", "test_game", "Village");
  write_valid(root / "slot_02.json", "test_game", "Cave");
  {
    std::ofstream f(root / "slot_03.json");
    f << "not json";
  }
  write_save_file(root / "slot_04.json", json{{"version", 999}, {"game_id", "test_game"}, {"mode", "single_map"}});
  write_valid(root / "slot_05.json", "other_game", "Elsewhere");

  const std::vector<corundum::save::SaveSlotInfo> rows = corundum::save::list_saves(root, "test_game");
  REQUIRE(rows.size() == 5);

  CHECK(rows[0].slot_id == "slot_01");
  REQUIRE(rows[0].meta.has_value());
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access): the REQUIRE above aborts the case when empty.
  const corundum::save::SaveMeta &meta0 = rows[0].meta.value();
  CHECK(meta0.location_name == "Village");
  CHECK(meta0.playtime_seconds == 42);
  CHECK(rows[0].error.empty());

  CHECK(rows[1].slot_id == "slot_02");
  REQUIRE(rows[1].meta.has_value());
  CHECK(rows[1].error.empty());

  CHECK(rows[2].slot_id == "slot_03");
  CHECK_FALSE(rows[2].meta.has_value());
  CHECK_FALSE(rows[2].error.empty());

  CHECK(rows[3].slot_id == "slot_04");
  CHECK_FALSE(rows[3].meta.has_value());
  CHECK(rows[3].error.find("version") != std::string::npos);

  CHECK(rows[4].slot_id == "slot_05");
  CHECK_FALSE(rows[4].meta.has_value());
  CHECK(rows[4].error.find("other_game") != std::string::npos);

  // A missing manual slot, and the always-absent autosave/quicksave, produce no rows.
  CHECK_FALSE(std::ranges::any_of(rows, [](const corundum::save::SaveSlotInfo &r) { return r.slot_id == "slot_06"; }));
  CHECK_FALSE(std::ranges::any_of(rows, [](const corundum::save::SaveSlotInfo &r) { return r.slot_id == "autosave"; }));
}

// ── Gameplay quick save/load and autosave ─────────────────────────────────────

TEST_CASE("gameplay: autosave writes the autosave slot and sets last_slot") {
  const ScratchUserData user_data;

  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(fs::is_directory(fixtures));
  REQUIRE(engine.initialize(make_world_config(fixtures)).has_value());
  corundum::gameplay::Gameplay gameplay{engine};
  gameplay.playtime_seconds = 12.0;

  const auto result = gameplay.autosave();
  REQUIRE(result.has_value());
  CHECK(gameplay.last_slot == "autosave");

  const auto dir = corundum::save::saves_directory(engine.cfg);
  REQUIRE(dir.has_value());
  const fs::path file = corundum::save::slot_path(*dir, corundum::save::k_autosave_slot);
  REQUIRE(fs::exists(file));

  const auto read = corundum::core::read_json(file);
  REQUIRE(read.has_value());
  const auto state = corundum::save::parse(*read);
  REQUIRE(state.has_value());
  CHECK(state->meta.location_name == engine.scene.zone_id); // no location registry loaded in this fixture
  CHECK(state->meta.playtime_seconds == 12);

  engine.cleanup();
}

TEST_CASE("gameplay: the QuickSave action writes the quicksave slot and toasts success") {
  const ScratchUserData user_data;

  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(fs::is_directory(fixtures));
  REQUIRE(engine.initialize(make_world_config(fixtures)).has_value());
  corundum::gameplay::Gameplay gameplay{engine};

  // fixed_step handles Action::QuickSave, so the framework owns the F5 shortcut.
  corundum::input::InputState state{};
  state.pressed.set(static_cast<std::size_t>(corundum::input::Action::QuickSave));
  engine.input_state = state;
  gameplay.fixed_step(1.f / 60.f);

  CHECK(gameplay.last_slot == "quicksave");
  REQUIRE(engine.toasts.size() == 1);
  CHECK(engine.toasts.at(0).text == "Game saved");

  const auto dir = corundum::save::saves_directory(engine.cfg);
  REQUIRE(dir.has_value());
  CHECK(fs::exists(corundum::save::slot_path(*dir, corundum::save::k_quicksave_slot)));

  engine.cleanup();
}

TEST_CASE("gameplay: a failed QuickLoad action raises a failure toast") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(fs::is_directory(fixtures));
  REQUIRE(engine.initialize(make_world_config(fixtures)).has_value());
  corundum::gameplay::Gameplay gameplay{engine};

  // An empty game_id makes saves_directory fail, so the load never touches the filesystem.
  engine.cfg.game_id.clear();
  corundum::input::InputState state{};
  state.pressed.set(static_cast<std::size_t>(corundum::input::Action::QuickLoad));
  engine.input_state = state;
  gameplay.fixed_step(1.f / 60.f);

  CHECK(gameplay.last_slot.empty());
  REQUIRE(engine.toasts.size() == 1);
  CHECK(engine.toasts.at(0).text.find("Load failed") != std::string::npos);

  engine.cleanup();
}
