#include <doctest/doctest.h>

#include <corundum/core/game_config.hpp>
#include <corundum/engine.hpp>
#include <corundum/gameplay/world/map_view.hpp>
#include <corundum/gameplay/world/portals/portal.hpp>
#include <corundum/gameplay/world/tilemap/world_manifest.hpp>
#include <corundum/gameplay/world/transition.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/platform/null/null_platform.hpp>
#include <corundum/render/data/render_state.hpp>

#include <expected>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

  corundum::core::GameConfig make_world_config(const fs::path &fixtures) {
    corundum::core::GameConfig cfg{};
    cfg.window_title = "world_transition_test";
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

  void adopt_platform(corundum::Engine &engine, unsigned w, unsigned h) {
    corundum::platform::null::NullPlatform platform = corundum::platform::null::make_null_platform(w, h);
    corundum::platform::null::adopt_null_platform(engine, platform);
  }

  /// Read the player's tile col/row from the scene transforms.
  std::pair<int, int> player_tile(const corundum::Engine &engine) {
    const auto &transforms = engine.scene.world.transforms;
    return {static_cast<int>(transforms.pos_col(engine.scene.player)),
            static_cast<int>(transforms.pos_row(engine.scene.player))};
  }

  std::string interior_path(const fs::path &fixtures) {
    return (fixtures / "tilemaps/interior.json").string();
  }

  /// Run one fixed simulation step (physics + portal detection) with no input.
  void advance(corundum::Engine &engine) {
    auto map = corundum::gameplay::world::build_map_view(engine.render, engine.cfg);
    corundum::input::InputState input{};
    corundum::gameplay::world::update(engine.scene, engine.cfg, engine.graphs, input, map, 1.f / 60.f,
                                      static_cast<float>(engine.win_w), static_cast<float>(engine.win_h), engine.flags,
                                      &engine.quests);
  }

  /// Place the player entity at an exact tile (row/col) without pathing.
  void move_player_to(corundum::Engine &engine, float col, float row) {
    auto &transforms = engine.scene.world.transforms;
    const auto slot = transforms.dense_idx(engine.scene.player);
    transforms.col[slot] = col;
    transforms.row[slot] = row;
    engine.scene.path.clear();
  }

} // namespace

using corundum::gameplay::world::handle_map_transition;
using corundum::gameplay::world::MapTransition;
using corundum::render::data::RenderMode;

// Boot a 2×2 chunk world (chunk_size 8). Default centre tile is (8,8); the default
// streaming window centre is chunk (1,1).

TEST_CASE("world transition — boot lands in World mode at the manifest centre") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(fs::is_directory(fixtures));

  REQUIRE(corundum::initialize(engine, make_world_config(fixtures)).has_value());

  CHECK(engine.render.mode == RenderMode::World);
  CHECK(player_tile(engine) == std::pair{8, 8});
  CHECK_FALSE(engine.entered_from_world);

  corundum::cleanup(engine);
}

TEST_CASE("world transition — World cross-map portal marks the journey and enters interior") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(corundum::initialize(engine, make_world_config(fixtures)).has_value());
  REQUIRE(player_tile(engine) == std::pair{8, 8});

  // Step onto the cave-mouth portal: a cross-map portal while in World mode.
  engine.scene.pending_transition = MapTransition{interior_path(fixtures), 1, 2, false};
  handle_map_transition(engine);

  CHECK(engine.render.mode == RenderMode::SingleMap);
  CHECK(player_tile(engine) == std::pair{1, 2});
  // Entering an interior from the world records the journey so a later exit can return.
  CHECK(engine.entered_from_world);

  corundum::cleanup(engine);
}

TEST_CASE("world transition — nested SingleMap cross-map does not clear the marker") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(corundum::initialize(engine, make_world_config(fixtures)).has_value());

  engine.scene.pending_transition = MapTransition{interior_path(fixtures), 1, 2, false};
  handle_map_transition(engine);
  REQUIRE(engine.entered_from_world);

  // A deeper interior (SingleMap → SingleMap) happens below the overworld boundary and
  // must not clear the journey marker.
  engine.scene.pending_transition = MapTransition{interior_path(fixtures), 5, 6, false};
  handle_map_transition(engine);

  CHECK(engine.render.mode == RenderMode::SingleMap);
  CHECK(engine.entered_from_world);

  corundum::cleanup(engine);
}

TEST_CASE("world transition — return_to_world exits interior, re-centres window, clears marker") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(corundum::initialize(engine, make_world_config(fixtures)).has_value());
  REQUIRE(player_tile(engine) == std::pair{8, 8});

  engine.scene.pending_transition = MapTransition{interior_path(fixtures), 1, 2, false};
  handle_map_transition(engine);
  REQUIRE(engine.render.mode == RenderMode::SingleMap);

  // Exit the interior: a return_to_world portal bearing the overworld return tile (12,3),
  // which lies in chunk (1,0) — off the manifest centre (1,1), proving window re-centering.
  engine.scene.pending_transition = MapTransition{"", 12, 3, true};
  handle_map_transition(engine);

  CHECK(engine.render.mode == RenderMode::World);
  CHECK(player_tile(engine) == std::pair{12, 3});
  CHECK_FALSE(engine.entered_from_world);
  CHECK(engine.render.chunks.last_center() == corundum::gameplay::world::tilemap::ChunkCoord{1, 0});

  corundum::cleanup(engine);
}

TEST_CASE("world transition — single-map boot with a return_to_world portal is ignored, not a quit") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  corundum::core::GameConfig cfg = make_world_config(fixtures);
  cfg.paths.world_manifest_path.clear(); // single-map mode: no overworld to return to
  cfg.paths.tilemap_path = interior_path(fixtures);
  REQUIRE(corundum::initialize(engine, std::move(cfg)).has_value());
  REQUIRE(engine.render.mode == RenderMode::SingleMap);

  engine.scene.pending_transition = MapTransition{"", 12, 3, true};
  handle_map_transition(engine);

  // No overworld exists, so the portal is ignored rather than terminating the process.
  CHECK_FALSE(engine.quit);
  CHECK(engine.render.mode == RenderMode::SingleMap);
  CHECK_FALSE(engine.entered_from_world);

  corundum::cleanup(engine);
}

TEST_CASE("world transition — end-to-end: walking onto the fixture portal round-trips") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(corundum::initialize(engine, make_world_config(fixtures)).has_value());
  REQUIRE(player_tile(engine) == std::pair{8, 8});

  // The cave-mouth portal lives in chunk (1,1) at local tile (5,5); build_map_view
  // offsets it by the chunk origin (8,8), so it activates at world tile (13,13).
  move_player_to(engine, 13.f, 13.f);
  advance(engine);

  // Physics surfaced a cross-map portal with return_to_world=false and the interior target.
  REQUIRE(engine.scene.pending_transition.has_value());
  const auto enter = *engine.scene.pending_transition;
  CHECK_FALSE(enter.return_to_world);
  // The fixture portal authors the target as a repo-relative path, which load_map
  // resolves against the working directory (the repo root when running tests).
  CHECK(enter.target_map == "tests/fixtures/tilemaps/interior.json");
  CHECK(enter.spawn_col == 1);
  CHECK(enter.spawn_row == 2);
  CHECK_FALSE(engine.entered_from_world); // not yet marked; marking happens on the transition

  handle_map_transition(engine);
  CHECK(engine.render.mode == RenderMode::SingleMap);
  CHECK(player_tile(engine) == std::pair{1, 2});
  REQUIRE(engine.entered_from_world);
  REQUIRE_FALSE(engine.scene.pending_transition.has_value());

  // inside the interior, walk onto its return_to_world portal at tile (0,0).
  move_player_to(engine, 0.f, 0.f);
  advance(engine);
  REQUIRE(engine.scene.pending_transition.has_value());
  const auto exit = *engine.scene.pending_transition;
  CHECK(exit.return_to_world);
  CHECK(exit.spawn_col == 12);
  CHECK(exit.spawn_row == 3);
  REQUIRE(engine.entered_from_world); // marker survives the interior step

  handle_map_transition(engine);
  CHECK(engine.render.mode == RenderMode::World);
  CHECK(player_tile(engine) == std::pair{12, 3});
  CHECK_FALSE(engine.entered_from_world);
  CHECK(engine.render.chunks.last_center() == corundum::gameplay::world::tilemap::ChunkCoord{1, 0});

  corundum::cleanup(engine);
}

TEST_CASE("world transition — chunk-to-chunk portal still teleports, not a scene transition") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(corundum::initialize(engine, make_world_config(fixtures)).has_value());
  REQUIRE(player_tile(engine) == std::pair{8, 8});

  // chunk_0_0 hosts a chunk-to-chunk portal at local (2,2) targeting chunk (1,1) at local
  // spawn (3,3); build_map_view offsets it to world (2,2) → spawn world (11,11).
  move_player_to(engine, 2.f, 2.f);
  advance(engine);

  // Chunk-to-chunk portals never set a pending_transition; physics teleports the player.
  CHECK_FALSE(engine.scene.pending_transition.has_value());
  CHECK(player_tile(engine) == std::pair{11, 11});

  corundum::cleanup(engine);
}
