#include <doctest/doctest.h>

#include <corundum/core/game_config.hpp>
#include <corundum/engine.hpp>
#include <corundum/gameplay/world/map_view.hpp>
#include <corundum/gameplay/world/portals/portal.hpp>
#include <corundum/gameplay/world/tilemap/world_manifest.hpp>
#include <corundum/gameplay/world/transition.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/platform/null/null_platform.hpp>
#include <corundum/render/render_state.hpp>

#include <expected>
#include <filesystem>
#include <optional>
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

  /// Like make_world_config but with the 5×1 streaming-world manifest.
  corundum::core::GameConfig make_streaming_config(const fs::path &fixtures) {
    corundum::core::GameConfig cfg = make_world_config(fixtures);
    cfg.window_title = "streaming_test";
    cfg.paths.world_manifest_path = (fixtures / "worlds/streaming/manifest.json").string();
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

  /// Run one fixed simulation step with a single action's `pressed` bit set. Used to drive
  /// the portal-confirm prompt's Select/Cancel path through update_transition_prompt().
  void advance_with(corundum::Engine &engine, corundum::input::Action action) {
    auto map = corundum::gameplay::world::build_map_view(engine.render, engine.cfg);
    corundum::input::InputState input{};
    input.pressed.set(static_cast<std::size_t>(action));
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

  /// Scan live transforms for an entity standing exactly at tile (col, row).
  std::optional<corundum::gameplay::entity::EntityId> entity_at(const corundum::Engine &engine, int col, int row) {
    const auto &transforms = engine.scene.world.transforms;
    for (const auto eid : transforms.active_entities())
      if (static_cast<int>(transforms.pos_col(eid)) == col && static_cast<int>(transforms.pos_row(eid)) == row)
        return eid;
    return std::nullopt;
  }

} // namespace

using corundum::gameplay::world::handle_map_transition;
using corundum::gameplay::world::MapTransition;
using corundum::render::RenderMode;

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

TEST_CASE("inventory — I toggles the panel and freezes the player, arrows move the cursor") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(corundum::initialize(engine, make_world_config(fixtures)).has_value());
  using corundum::gameplay::world::GameMode;
  REQUIRE(engine.scene.mode == GameMode::Exploring);
  REQUIRE(engine.scene.inventory_cursor == 0);

  // Three held items → three rows to wrap within.
  engine.flags["item.a"] = 1;
  engine.flags["item.b"] = 2;
  engine.flags["item.c"] = 1;

  // Press I: Exploring → Inventory, cursor reset.
  advance_with(engine, corundum::input::Action::Inventory);
  CHECK(engine.scene.mode == GameMode::Inventory);
  CHECK(engine.scene.inventory_cursor == 0);

  // Arrows move the highlight while paused.
  advance_with(engine, corundum::input::Action::MoveDown);
  CHECK(engine.scene.mode == GameMode::Inventory);
  CHECK(engine.scene.inventory_cursor == 1);
  advance_with(engine, corundum::input::Action::MoveUp);
  CHECK(engine.scene.inventory_cursor == 0);

  // Down past the last row wraps to the first (dialogue choice-list behaviour).
  advance_with(engine, corundum::input::Action::MoveDown);
  advance_with(engine, corundum::input::Action::MoveDown);
  advance_with(engine, corundum::input::Action::MoveDown);
  CHECK(engine.scene.inventory_cursor == 0);

  // Up past the first row wraps to the last.
  advance_with(engine, corundum::input::Action::MoveUp);
  CHECK(engine.scene.inventory_cursor == 2);

  // Press I again: Inventory → Exploring.
  advance_with(engine, corundum::input::Action::Inventory);
  CHECK(engine.scene.mode == GameMode::Exploring);

  // Esc also closes an open panel.
  advance_with(engine, corundum::input::Action::Inventory);
  REQUIRE(engine.scene.mode == GameMode::Inventory);
  advance_with(engine, corundum::input::Action::Cancel);
  CHECK(engine.scene.mode == GameMode::Exploring);

  // Opening again resets the cursor to the top.
  advance_with(engine, corundum::input::Action::MoveDown);
  advance_with(engine, corundum::input::Action::Inventory);
  CHECK(engine.scene.mode == GameMode::Inventory);
  CHECK(engine.scene.inventory_cursor == 0);

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

  // Stepping onto the portal pauses the player on a confirm prompt — no automatic transition.
  using corundum::gameplay::world::GameMode;
  REQUIRE(engine.scene.mode == GameMode::Prompt);
  REQUIRE(engine.scene.transition_prompt.has_value());
  CHECK_FALSE(engine.scene.transition_prompt->declined());
  REQUIRE_FALSE(engine.scene.pending_transition.has_value());
  CHECK_FALSE(engine.entered_from_world); // not yet marked; marking happens on the transition

  // Confirm — now the candidate transitions into the actual pending_transition.
  advance_with(engine, corundum::input::Action::Select);
  REQUIRE(engine.scene.mode == GameMode::Exploring);
  REQUIRE(engine.scene.pending_transition.has_value());
  REQUIRE_FALSE(engine.scene.transition_prompt.has_value());
  const auto enter = *engine.scene.pending_transition;
  CHECK_FALSE(enter.return_to_world);
  // The fixture portal authors the target as a repo-relative path, which load_map
  // resolves against the working directory (the repo root when running tests).
  CHECK(enter.target_map == "tests/fixtures/tilemaps/interior.json");
  CHECK(enter.spawn_col == 1);
  CHECK(enter.spawn_row == 2);

  handle_map_transition(engine);
  CHECK(engine.render.mode == RenderMode::SingleMap);
  CHECK(player_tile(engine) == std::pair{1, 2});
  REQUIRE(engine.entered_from_world);
  REQUIRE_FALSE(engine.scene.pending_transition.has_value());

  // inside the interior, walk onto its return_to_world portal at tile (0,0).
  move_player_to(engine, 0.f, 0.f);
  advance(engine);
  REQUIRE(engine.scene.mode == GameMode::Prompt);
  REQUIRE(engine.scene.transition_prompt.has_value());
  CHECK(engine.scene.transition_prompt->transition().return_to_world);
  REQUIRE(engine.entered_from_world); // marker survives the interior step

  advance_with(engine, corundum::input::Action::Select);
  REQUIRE(engine.scene.pending_transition.has_value());
  const auto exit = *engine.scene.pending_transition;
  CHECK(exit.return_to_world);
  CHECK(exit.spawn_col == 12);
  CHECK(exit.spawn_row == 3);

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

TEST_CASE("world transition — pick_tile resolves a tile in world mode (hover works)") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(corundum::initialize(engine, make_world_config(fixtures)).has_value());
  REQUIRE(player_tile(engine) == std::pair{8, 8});

  const auto map = corundum::gameplay::world::build_map_view(engine.render, engine.cfg);
  // World-mode MapView carries world_render (not elevation_map); pick_tile must use it.
  REQUIRE(map.world_render != nullptr);
  CHECK(map.elevation_map == nullptr);

  const corundum::gameplay::world::Camera &camera = engine.scene.camera;
  // The camera is centred on the player tile (8,8); the viewport centre must fall on a tile.
  const float mouse_x = 160.f;
  const float mouse_y = 120.f;
  const auto result = corundum::gameplay::sys::pick_tile(mouse_x, mouse_y, camera, map,
                                                         engine.cfg.elevation_step_px * map.tile_scale, camera.zoom);

  CHECK(result.has_value());

  corundum::cleanup(engine);
}

TEST_CASE("world transition — stepping on a portal surfaces a confirm prompt, no auto-transition") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(corundum::initialize(engine, make_world_config(fixtures)).has_value());
  REQUIRE(player_tile(engine) == std::pair{8, 8});

  // The cave-mouth portal lives at world (13,13) — see test_world_transition fixture notes.
  move_player_to(engine, 13.f, 13.f);
  advance(engine);

  using corundum::gameplay::world::GameMode;
  CHECK(engine.scene.mode == GameMode::Prompt);
  REQUIRE(engine.scene.transition_prompt.has_value());
  CHECK_FALSE(engine.scene.transition_prompt->declined());
  CHECK(engine.scene.transition_prompt->confirm_selected()); // default highlight: Yes
  // pending_transition stays empty until the player confirms — handle_map_transition is a no-op.
  CHECK_FALSE(engine.scene.pending_transition.has_value());
  handle_map_transition(engine);
  CHECK(engine.render.mode == RenderMode::World);
  CHECK(player_tile(engine) == std::pair{13, 13});

  corundum::cleanup(engine);
}

TEST_CASE("world transition — Select on the prompt promotes the stashed transition") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(corundum::initialize(engine, make_world_config(fixtures)).has_value());
  REQUIRE(player_tile(engine) == std::pair{8, 8});

  move_player_to(engine, 13.f, 13.f);
  advance(engine);
  using corundum::gameplay::world::GameMode;
  REQUIRE(engine.scene.mode == GameMode::Prompt);

  advance_with(engine, corundum::input::Action::Select);

  CHECK(engine.scene.mode == GameMode::Exploring);
  REQUIRE(engine.scene.pending_transition.has_value());
  CHECK_FALSE(engine.scene.transition_prompt.has_value());

  // Round-trip the existing assertions to prove the promoted transition is the same one
  // the old auto-fire path would have produced.
  handle_map_transition(engine);
  CHECK(engine.render.mode == RenderMode::SingleMap);
  CHECK(player_tile(engine) == std::pair{1, 2});
  CHECK(engine.entered_from_world);

  corundum::cleanup(engine);
}

TEST_CASE("world transition — Cancel on the prompt suppresses re-prompt until player leaves the rect") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(corundum::initialize(engine, make_world_config(fixtures)).has_value());
  REQUIRE(player_tile(engine) == std::pair{8, 8});

  move_player_to(engine, 13.f, 13.f);
  advance(engine);
  using corundum::gameplay::world::GameMode;
  REQUIRE(engine.scene.mode == GameMode::Prompt);

  advance_with(engine, corundum::input::Action::Cancel);
  CHECK(engine.scene.mode == GameMode::Exploring);
  REQUIRE(engine.scene.transition_prompt.has_value());
  CHECK(engine.scene.transition_prompt->declined());
  CHECK_FALSE(engine.scene.pending_transition.has_value());

  // Still standing on the portal: advance() must NOT re-arm the prompt or set pending_transition.
  advance(engine);
  CHECK(engine.scene.mode == GameMode::Exploring);
  CHECK_FALSE(engine.scene.pending_transition.has_value());
  REQUIRE(engine.scene.transition_prompt.has_value());
  CHECK(engine.scene.transition_prompt->declined());
  advance(engine);
  CHECK_FALSE(engine.scene.pending_transition.has_value());
  CHECK(engine.scene.transition_prompt.has_value());
  CHECK(engine.scene.transition_prompt->declined());

  // Walk off the portal: the next advance() resets the declined prompt.
  move_player_to(engine, 8.f, 8.f);
  advance(engine);
  CHECK_FALSE(engine.scene.transition_prompt.has_value());

  // Walk back on: the prompt re-arms.
  move_player_to(engine, 13.f, 13.f);
  advance(engine);
  CHECK(engine.scene.mode == GameMode::Prompt);
  REQUIRE(engine.scene.transition_prompt.has_value());
  CHECK_FALSE(engine.scene.transition_prompt->declined());

  corundum::cleanup(engine);
}

TEST_CASE("world transition — return_to_world portal prompts with return_to_world=true") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(corundum::initialize(engine, make_world_config(fixtures)).has_value());
  REQUIRE(player_tile(engine) == std::pair{8, 8});

  // Enter the interior via the existing path (no prompt step needed — the test focuses on the
  // return leg's prompt and confirm semantics, not the entry flow already covered above).
  engine.scene.pending_transition = MapTransition{interior_path(fixtures), 1, 2, false};
  handle_map_transition(engine);
  REQUIRE(engine.render.mode == RenderMode::SingleMap);
  REQUIRE(engine.entered_from_world);

  // Walk onto the interior's return_to_world portal at tile (0,0).
  move_player_to(engine, 0.f, 0.f);
  advance(engine);
  using corundum::gameplay::world::GameMode;
  REQUIRE(engine.scene.mode == GameMode::Prompt);
  REQUIRE(engine.scene.transition_prompt.has_value());
  CHECK(engine.scene.transition_prompt->transition().return_to_world);
  CHECK_FALSE(engine.scene.pending_transition.has_value());

  advance_with(engine, corundum::input::Action::Select);
  REQUIRE(engine.scene.pending_transition.has_value());
  CHECK(engine.scene.mode == GameMode::Exploring);

  handle_map_transition(engine);
  CHECK(engine.render.mode == RenderMode::World);
  CHECK(player_tile(engine) == std::pair{12, 3});
  CHECK_FALSE(engine.entered_from_world);
  CHECK(engine.render.chunks.last_center() == corundum::gameplay::world::tilemap::ChunkCoord{1, 0});

  corundum::cleanup(engine);
}

TEST_CASE("world transition — Left/Right navigates the Yes/No highlight") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(corundum::initialize(engine, make_world_config(fixtures)).has_value());
  REQUIRE(player_tile(engine) == std::pair{8, 8});

  move_player_to(engine, 13.f, 13.f);
  advance(engine);
  using corundum::gameplay::world::GameMode;
  REQUIRE(engine.scene.mode == GameMode::Prompt);
  REQUIRE(engine.scene.transition_prompt.has_value());
  CHECK(engine.scene.transition_prompt->confirm_selected());

  // Right → No highlighted, still in prompt, no transition yet.
  advance_with(engine, corundum::input::Action::MoveRight);
  CHECK(engine.scene.mode == GameMode::Prompt);
  CHECK_FALSE(engine.scene.transition_prompt->confirm_selected());
  CHECK_FALSE(engine.scene.pending_transition.has_value());

  // Left → Yes highlighted again.
  advance_with(engine, corundum::input::Action::MoveLeft);
  CHECK(engine.scene.mode == GameMode::Prompt);
  CHECK(engine.scene.transition_prompt->confirm_selected());
  CHECK_FALSE(engine.scene.pending_transition.has_value());

  // Up/Down alias — Down acts like Right, Up like Left.
  advance_with(engine, corundum::input::Action::MoveDown);
  CHECK_FALSE(engine.scene.transition_prompt->confirm_selected());
  advance_with(engine, corundum::input::Action::MoveUp);
  CHECK(engine.scene.transition_prompt->confirm_selected());

  corundum::cleanup(engine);
}

TEST_CASE("world transition — Select on No backs out like Cancel") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(corundum::initialize(engine, make_world_config(fixtures)).has_value());
  REQUIRE(player_tile(engine) == std::pair{8, 8});

  move_player_to(engine, 13.f, 13.f);
  advance(engine);
  using corundum::gameplay::world::GameMode;
  REQUIRE(engine.scene.mode == GameMode::Prompt);

  advance_with(engine, corundum::input::Action::MoveRight);
  REQUIRE_FALSE(engine.scene.transition_prompt->confirm_selected());

  advance_with(engine, corundum::input::Action::Select);
  CHECK(engine.scene.mode == GameMode::Exploring);
  REQUIRE(engine.scene.transition_prompt.has_value());
  CHECK(engine.scene.transition_prompt->declined());
  CHECK_FALSE(engine.scene.pending_transition.has_value());

  corundum::cleanup(engine);
}

TEST_CASE("world transition — re-arming the prompt resets Yes as the default highlight") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(corundum::initialize(engine, make_world_config(fixtures)).has_value());
  REQUIRE(player_tile(engine) == std::pair{8, 8});

  move_player_to(engine, 13.f, 13.f);
  advance(engine);
  using corundum::gameplay::world::GameMode;
  REQUIRE(engine.scene.mode == GameMode::Prompt);
  REQUIRE(engine.scene.transition_prompt->confirm_selected());

  // Move the highlight to No, then cancel — physics clears the prompt once the player walks off.
  advance_with(engine, corundum::input::Action::MoveRight);
  advance_with(engine, corundum::input::Action::Cancel);
  REQUIRE(engine.scene.transition_prompt.has_value());
  REQUIRE(engine.scene.transition_prompt->declined());
  // (declined flag doesn't change the highlight; the prompt itself is reset on re-arm.)

  move_player_to(engine, 8.f, 8.f);
  advance(engine);
  CHECK_FALSE(engine.scene.transition_prompt.has_value());

  move_player_to(engine, 13.f, 13.f);
  advance(engine);
  REQUIRE(engine.scene.mode == GameMode::Prompt);
  REQUIRE(engine.scene.transition_prompt.has_value());
  CHECK(engine.scene.transition_prompt->confirm_selected()); // Yes again, not carried over

  corundum::cleanup(engine);
}

TEST_CASE("world transition — world boot spawns per-chunk actors") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(corundum::initialize(engine, make_world_config(fixtures)).has_value());
  REQUIRE(player_tile(engine) == std::pair{8, 8});

  // The a00 greeter in chunk (0,0) is the only per-chunk actor with a dialogue ref.
  CHECK(engine.scene.world.dialogue_refs.count == 1);

  // All four chunks of the 2×2 fixture world are resident at boot.
  CHECK(engine.scene.chunk_actors.size() == 4);

  // a00 (chunk 0,0 local 2,3) and a11 (chunk 1,1 origin 8,8 + local 1,1 → 9,9).
  const auto a00 = entity_at(engine, 2, 3);
  const auto a11 = entity_at(engine, 9, 9);
  REQUIRE(a00.has_value());
  REQUIRE(a11.has_value());
  CHECK_FALSE(*a00 == *a11);

  corundum::cleanup(engine);
}

TEST_CASE("world transition — world actors return after an interior round-trip") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(corundum::initialize(engine, make_world_config(fixtures)).has_value());
  REQUIRE(player_tile(engine) == std::pair{8, 8});

  // Enter the interior (records the world journey), then exit back to the overworld.
  engine.scene.pending_transition = MapTransition{interior_path(fixtures), 1, 2, false};
  handle_map_transition(engine);
  REQUIRE(engine.render.mode == RenderMode::SingleMap);
  REQUIRE(engine.entered_from_world);

  engine.scene.pending_transition = MapTransition{"", 12, 3, true};
  handle_map_transition(engine);
  REQUIRE(engine.render.mode == RenderMode::World);
  CHECK(player_tile(engine) == std::pair{12, 3});

  // Chunk actors are repopulated at the same tiles (the whole world was rebuilt).
  CHECK(engine.scene.chunk_actors.size() == 4);
  const auto back_a00 = entity_at(engine, 2, 3);
  const auto back_a11 = entity_at(engine, 9, 9);
  REQUIRE(back_a00.has_value());
  REQUIRE(back_a11.has_value());
  CHECK(engine.scene.world.entities.is_live(*back_a00));
  CHECK(engine.scene.world.entities.is_live(*back_a11));

  corundum::cleanup(engine);
}

TEST_CASE("world streaming — actors spawn only for resident chunks") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(corundum::initialize(engine, make_streaming_config(fixtures)).has_value());

  // 5×1 world boots at the manifest centre (20,4) → chunk (2,0); the 3×1 window
  // covers (1,0)(2,0)(3,0) — chunks (0,0) and (4,0) are not resident at boot.
  REQUIRE(player_tile(engine) == std::pair{20, 4});
  CHECK(engine.render.chunks.active_size() == 3);
  CHECK(engine.scene.chunk_actors.size() == 3);

  // a30 lives in resident chunk (3,0); a00 lives in non-resident chunk (0,0).
  const auto a30 = entity_at(engine, 4 + 3 * 8, 4);
  REQUIRE(a30.has_value());
  CHECK_FALSE(entity_at(engine, 2, 3).has_value());

  corundum::cleanup(engine);
}

TEST_CASE("world streaming — streaming a chunk out despawns its actors, streaming back respawns") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(corundum::initialize(engine, make_streaming_config(fixtures)).has_value());
  REQUIRE(player_tile(engine) == std::pair{20, 4});

  // Capture the boot a30 handle before any streaming churn.
  const auto boot_a30 = entity_at(engine, 4 + 3 * 8, 4);
  REQUIRE(boot_a30.has_value());

  // Walk the player west past the 2.56-tile hysteresis margin so the window
  // recentres on chunk (0,0) (local col 3 < margin 2.56) and chunks (1,0)/(2,0)
  // prune. Chunk (0,0) loads one-per-frame via load_one_pending_chunk.
  move_player_to(engine, 3.f, 4.f);
  for (int i = 0; i < 30; ++i) {
    engine.timer.accumulator = engine.timer.target_dt;
    REQUIRE(corundum::run_frame(engine));
  }

  // Chunk (0,0) streamed in: its a00 actor exists now.
  const auto west_a00 = entity_at(engine, 2, 3);
  REQUIRE(west_a00.has_value());
  // Chunk (3,0) streamed out: its a30 actor is gone.
  CHECK_FALSE(engine.scene.world.entities.is_live(*boot_a30));
  CHECK_FALSE(entity_at(engine, 4 + 3 * 8, 4).has_value());

  // Walk back east to tile 20; the window recentres on chunk (2,0) and (3,0) reloads.
  move_player_to(engine, 20.f, 4.f);
  for (int i = 0; i < 30; ++i) {
    engine.timer.accumulator = engine.timer.target_dt;
    REQUIRE(corundum::run_frame(engine));
  }

  const auto back_a30 = entity_at(engine, 4 + 3 * 8, 4);
  REQUIRE(back_a30.has_value());
  CHECK(engine.scene.world.entities.is_live(*back_a30));

  corundum::cleanup(engine);
}
