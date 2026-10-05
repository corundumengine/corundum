// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include "font_fixtures.hpp"
#include "temp_dir.hpp"
#include <corundum/core/game_config.hpp>
#include <corundum/engine.hpp>
#include <corundum/entities/world.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/platform/null/null_platform.hpp>
#include <corundum/platform/null/null_window.hpp>
#include <corundum/save/save.hpp>
#include <corundum/world/scene.hpp>
#include <corundum/world/tilemap/world_manifest.hpp>

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

  /// Single-map config matching the lifecycle fixtures (interior tilemap).
  corundum::core::GameConfig make_single_map_config(const fs::path &fixtures) {
    corundum::core::GameConfig cfg{};
    cfg.window_title = "player_absent_test";
    cfg.win_w = 320.f;
    cfg.win_h = 240.f;
    cfg.paths.sprites_dir = (fixtures / "sprites").string();
    cfg.paths.tilemap_path = (fixtures / "tilemaps/lifecycle_test.json").string();
    cfg.paths.font_dir = (fixtures / "fonts").string();
    corundum::test::set_missing_fonts(cfg.paths);
    cfg.paths.world_manifest_path.clear();
    cfg.paths.spawn_points_dir = (fixtures / "spawn_points").string();
    cfg.paths.portals_dir = (fixtures / "portals").string();
    cfg.paths.dialogue_dir.clear();
    cfg.paths.quests_dir.clear();
    cfg.paths.sounds_dir.clear();
    return cfg;
  }

  /// Streaming-world config matching test_world_transition's 5×1 manifest.
  corundum::core::GameConfig make_streaming_config(const fs::path &fixtures) {
    corundum::core::GameConfig cfg{};
    cfg.window_title = "player_absent_streaming_test";
    cfg.win_w = 320.f;
    cfg.win_h = 240.f;
    cfg.paths.sprites_dir = (fixtures / "sprites").string();
    cfg.paths.font_dir = (fixtures / "fonts").string();
    corundum::test::set_missing_fonts(cfg.paths);
    cfg.paths.world_manifest_path = (fixtures / "worlds/streaming/manifest.json").string();
    cfg.paths.spawn_points_dir = (fixtures / "spawn_points").string();
    cfg.paths.portals_dir = (fixtures / "portals").string();
    cfg.paths.dialogue_dir.clear();
    cfg.paths.quests_dir.clear();
    cfg.paths.sounds_dir.clear();
    return cfg;
  }

  void adopt_platform(corundum::Engine &engine, unsigned w, unsigned h) {
    corundum::platform::null::NullPlatform platform = corundum::platform::null::make_null_platform(w, h);
    corundum::platform::null::adopt_null_platform(engine, platform);
  }

  corundum::platform::null::NullWindow *null_window(const corundum::Engine &engine) {
    return dynamic_cast<corundum::platform::null::NullWindow *>(engine.window.get());
  }

  /// Run one frame with @p dt of simulation owed.
  bool step(corundum::Engine &engine) {
    engine.timer.accumulator = engine.timer.target_dt;
    return engine.run_frame();
  }

} // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity): CHECK expands to branches.
TEST_CASE("player absent — single-map frames run without the player") {
  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(fs::is_directory(fixtures));

  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);
  REQUIRE(engine.initialize(make_single_map_config(fixtures)).has_value());
  REQUIRE(corundum::world::player_present(engine.scene));

  corundum::entities::despawn(engine.scene.world, engine.scene.player);
  REQUIRE_FALSE(corundum::world::player_present(engine.scene));

  // Held movement plus a click, both of which would otherwise read the player's row.
  corundum::platform::null::NullWindow *window = null_window(engine);
  window->scripted_keys.emplace_back(corundum::input::Key::D, true);
  window->scripted_mouse.emplace_back(corundum::input::MouseButton::Left, true);

  for (int i = 0; i < 5; ++i)
    CHECK(step(engine));

  engine.cleanup();
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): CHECK expands to branches.
TEST_CASE("player absent — streaming frames leave the window where it is") {
  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(fs::is_directory(fixtures));

  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);
  REQUIRE(engine.initialize(make_streaming_config(fixtures)).has_value());
  REQUIRE(corundum::world::player_present(engine.scene));

  const corundum::world::tilemap::ChunkCoord center = engine.render.chunks.last_center();

  corundum::entities::despawn(engine.scene.world, engine.scene.player);
  REQUIRE_FALSE(corundum::world::player_present(engine.scene));

  for (int i = 0; i < 5; ++i)
    CHECK(step(engine));

  CHECK(engine.render.chunks.last_center() == center);

  engine.cleanup();
}

TEST_CASE("player absent — save_game reports no player entity") {
  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(fs::is_directory(fixtures));

  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);
  REQUIRE(engine.initialize(make_single_map_config(fixtures)).has_value());

  corundum::entities::despawn(engine.scene.world, engine.scene.player);
  REQUIRE_FALSE(corundum::world::player_present(engine.scene));

  const corundum::test::TempDir tmp{"corundum_player_absent_", "save"};
  const auto result = corundum::save::save_game(engine, tmp / "save.json");

  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().find("no player") != std::string::npos);

  engine.cleanup();
}
