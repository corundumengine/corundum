// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include "font_fixtures.hpp"
#include "memory_content_source.hpp"
#include "render_state_fixtures.hpp"
#include "temp_dir.hpp"
#include "world_transition_fixtures.hpp"

#include "null_platform.hpp"
#include <corundum/core/game_config.hpp>
#include <corundum/engine.hpp>
#include <corundum/entities/components.hpp>
#include <corundum/entities/world.hpp>
#include <corundum/render/chunk_window.hpp>
#include <corundum/render/render_state.hpp>
#include <corundum/sprites/character_registry.hpp>
#include <corundum/world/actors/actor.hpp>
#include <corundum/world/spawn.hpp>
#include <corundum/world/tilemap/tilemap.hpp>
#include <corundum/world/tilemap/world_manifest.hpp>

#include <filesystem>
#include <format>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <string_view>
#include <utility>

using corundum::test::disk;

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
    cfg.win_w = 320;
    cfg.win_h = 240;
    cfg.paths.sprites_dir = (fixtures / "sprites").string();
    cfg.paths.font_dir = (fixtures / "fonts").string();
    corundum::test::set_missing_fonts(cfg.paths);
    cfg.paths.world_manifest_path = (fixtures / "worlds/transition/manifest.json").string();
    cfg.paths.spawn_points_dir = (fixtures / "spawn_points").string();
    cfg.paths.portals_dir = (fixtures / "portals").string();
    cfg.paths.dialogue_dir.clear();
    cfg.paths.quests_dir.clear();
    cfg.paths.sounds_dir.clear();
    return cfg;
  }

} // namespace

using corundum::test::initialize_with_world;
using corundum::world::load_spawn_points;

// ── Missing file ──────────────────────────────────────────────────────────────

TEST_CASE("load_spawn_points — missing file returns empty SpawnPoints") {
  const auto result = load_spawn_points(disk(), "/nonexistent/path/spawn.json");
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
  const auto result = load_spawn_points(disk(), p);
  REQUIRE(result.has_value());
  CHECK(!result->player.has_value());
  REQUIRE(result->actors.size() == 2);
  CHECK(result->actors[0].col == 2);
  CHECK(result->actors[0].sprite_name == "npc1");
  CHECK(result->actors[0].facing == "south");
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
  const auto result = load_spawn_points(disk(), p);
  REQUIRE(result.has_value());
  REQUIRE(result->player.has_value());
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access): REQUIRE above aborts the case when empty.
  const auto &player = result->player.value();
  CHECK(player.col == doctest::Approx(10.5f));
  CHECK(player.row == doctest::Approx(4.f));
  CHECK(result->actors.empty());
}

TEST_CASE("load_spawn_points — player.col zero is valid") {
  const auto dir = temp_dir("player_col_zero");
  const auto p = dir / "spawn.json";
  write_file(p, R"({
    "player": { "col": 0.0, "row": 0.0 },
    "actors": []
  })");
  const auto result = load_spawn_points(disk(), p);
  REQUIRE(result.has_value());
  REQUIRE(result->player.has_value());
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access): REQUIRE above aborts the case when empty.
  const auto &player = result->player.value();
  CHECK(player.col == doctest::Approx(0.f));
  CHECK(player.row == doctest::Approx(0.f));
}

TEST_CASE("load_spawn_points — player not an object returns error") {
  const auto dir = temp_dir("player_not_obj");
  const auto p = dir / "spawn.json";
  write_file(p, R"({ "player": 42, "actors": [] })");
  const auto result = load_spawn_points(disk(), p);
  REQUIRE_FALSE(result.has_value());
  CHECK_EQ(result.error(), std::format("Spawn points '{}' 'player' must be an object", p.string()));
}

TEST_CASE("load_spawn_points — player missing col returns error") {
  const auto dir = temp_dir("player_no_col");
  const auto p = dir / "spawn.json";
  write_file(p, R"({ "player": { "row": 5.0 }, "actors": [] })");
  const auto result = load_spawn_points(disk(), p);
  REQUIRE_FALSE(result.has_value());
  CHECK_EQ(result.error(), std::format("Spawn points '{}' 'player' missing or invalid 'col'/'row'", p.string()));
}

TEST_CASE("load_spawn_points — player negative col returns error") {
  const auto dir = temp_dir("player_neg_col");
  const auto p = dir / "spawn.json";
  write_file(p, R"({ "player": { "col": -1.0, "row": 5.0 }, "actors": [] })");
  const auto result = load_spawn_points(disk(), p);
  REQUIRE_FALSE(result.has_value());
  CHECK_EQ(result.error(), std::format("Spawn points '{}' 'player' 'col' and 'row' must be >= 0", p.string()));
}

TEST_CASE("load_spawn_points — malformed JSON returns error") {
  const auto dir = temp_dir("malformed");
  const auto p = dir / "spawn.json";
  write_file(p, "{bad json");
  const auto result = load_spawn_points(disk(), p);
  REQUIRE_FALSE(result.has_value());
  CHECK_MESSAGE(result.error().starts_with(std::format("malformed spawn points in {}: ", p.string())), result.error());
}

TEST_CASE("load_spawn_points — not an object returns error") {
  const auto dir = temp_dir("array");
  const auto p = dir / "spawn.json";
  write_file(p, "[]");
  const auto result = load_spawn_points(disk(), p);
  REQUIRE_FALSE(result.has_value());
  CHECK_EQ(result.error(), std::format("Spawn points '{}' must be a JSON object", p.string()));
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
  const auto result = load_spawn_points(disk(), p);
  REQUIRE(result.has_value());
  REQUIRE(result->actors.size() == 2);
  CHECK(result->actors[0].id == "brann");
  CHECK(result->actors[1].id.empty());
}

TEST_CASE("load_spawn_points — duplicate actor id returns error") {
  const auto dir = temp_dir("actor_id_duplicate");
  const auto p = dir / "spawn.json";
  write_file(p, R"({
    "actors": [
      { "col": 2, "row": 3, "sprite": "npc1", "id": "brann" },
      { "col": 5, "row": 7, "sprite": "npc2", "id": "brann" }
    ]
  })");
  const auto result = load_spawn_points(disk(), p);
  REQUIRE_FALSE(result.has_value());
  CHECK_EQ(result.error(), std::format("Spawn points '{}' actors[1] duplicate actor id 'brann'", p.string()));
}

TEST_CASE("load_spawn_points — invalid facing returns error") {
  const auto dir = temp_dir("bad_facing");
  const auto p = dir / "spawn.json";
  write_file(p, R"({
    "actors": [
      { "col": 2, "row": 3, "sprite": "npc1", "facing": "nroth" }
    ]
  })");
  const auto result = load_spawn_points(disk(), p);
  REQUIRE_FALSE(result.has_value());
  CHECK_EQ(result.error(), std::format("Spawn points '{}' actors[0] 'facing' is not a valid direction", p.string()));
}

TEST_CASE("load_spawn_points — facing has wrong type returns error") {
  const auto dir = temp_dir("facing_type");
  const auto p = dir / "spawn.json";
  write_file(p, R"({ "actors": [ { "col": 2, "row": 3, "sprite": "npc1", "facing": 7 } ] })");
  const auto result = load_spawn_points(disk(), p);
  REQUIRE_FALSE(result.has_value());
  CHECK_EQ(result.error(), std::format("Spawn points '{}' actors[0] 'facing' has wrong type", p.string()));
}

// ── find_actor wiring ─────────────────────────────────────────────────────────

TEST_CASE("find_actor — world boot populates actor_ids from spawn points") {
  corundum::Engine engine{};
  auto platform = corundum::platform::null::make_null_platform(320, 240);
  corundum::platform::null::adopt_null_platform(engine, platform);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(fs::is_directory(fixtures));
  REQUIRE(initialize_with_world(engine, make_world_config(fixtures)).has_value());

  // a00 (chunk 0,0) and a11 (chunk 1,1) carry ids in the fixture spawn points.
  const auto a00 = corundum::entities::find_actor(engine.scene.world, "a00");
  REQUIRE(a00.has_value());
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access): REQUIRE above aborts the case when empty.
  const auto a00_id = a00.value();
  CHECK(engine.scene.world.entities.is_live(a00_id));

  const auto a11 = corundum::entities::find_actor(engine.scene.world, "a11");
  REQUIRE(a11.has_value());
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access): REQUIRE above aborts the case when empty.
  const auto a11_id = a11.value();
  CHECK_FALSE(a11_id == a00_id);

  // The player entity and unknown ids have no row.
  CHECK_FALSE(corundum::entities::find_actor(engine.scene.world, "player").has_value());
  CHECK_FALSE(corundum::entities::find_actor(engine.scene.world, "missing").has_value());

  // Despawning the actor drops its id row.
  corundum::entities::despawn(engine.scene.world, a00_id);
  CHECK_FALSE(corundum::entities::find_actor(engine.scene.world, "a00").has_value());

  engine.cleanup();
}

// ── spawn_world / sync_chunk_actors ─────────────────────────────────────────────

namespace {

  /// Minimal character sheet exposing the two sprites the spawn code resolves by name.
  std::string_view two_sprite_sheet_json() {
    return R"({"id":"heroes","path":"game/assets/heroes.png","frame_width":32,"frame_height":32,"frames":{)"
           R"("hero_walk":{"col_span":1,"row_span":2,"footprint_col_span":0.25,"footprint_row_span":0.75,)"
           R"("south":[{"col":0,"row":0},{"col":1,"row":0}]},)"
           R"("hero_idle":{"col_span":1,"row_span":2,"footprint_col_span":0.25,"footprint_row_span":0.75,)"
           R"("south":[{"col":0,"row":0}]}}})";
  }

  corundum::core::GameConfig make_player_config(const fs::path &spawn_points_dir) {
    corundum::core::GameConfig cfg{};
    cfg.player.walk_sprite = "hero_walk";
    cfg.player.idle_sprite = "hero_idle";
    cfg.player.col = 9.f;
    cfg.player.row = 9.f;
    cfg.paths.spawn_points_dir = spawn_points_dir.string();
    return cfg;
  }

  corundum::world::tilemap::Tilemap make_tilemap(const fs::path &map_path) {
    corundum::world::tilemap::Tilemap tilemap;
    tilemap.path = map_path.string();
    return tilemap;
  }

  corundum::sprites::CharacterRegistry make_registry(const fs::path &dir) {
    write_file(dir / "characters" / "heroes.json", two_sprite_sheet_json());
    corundum::sprites::CharacterRegistry registry;
    REQUIRE(registry.load_all(disk(), dir).has_value());
    return registry;
  }

  /// A World-mode render state with @p coords resident and @p chunk_size tiles per chunk side.
  corundum::render::RenderState make_world_render(std::initializer_list<corundum::world::tilemap::ChunkCoord> coords,
                                                  int chunk_size) {
    corundum::render::RenderState render;
    auto &world_area = render.area.emplace<corundum::render::WorldArea>();
    world_area.manifest.chunk_size = chunk_size;
    for (const auto coord : coords) {
      corundum::render::ChunkEntry entry;
      entry.coord = coord;
      world_area.chunks.add_active(std::move(entry));
    }
    return render;
  }

} // namespace

TEST_CASE("spawn_world — explicit player_pos wins over per-map and game.json placement") {
  const auto dir = temp_dir("spawn_world_precedence");
  const auto registry = make_registry(dir);
  const auto spawn_dir = dir / "spawn_points";
  write_file(spawn_dir / "village.json", R"({"player":{"col":1.5,"row":2.5},"actors":[]})");

  const auto tilemap = make_tilemap(dir / "village.tmx");
  const auto explicit_pos = corundum::entities::Position{.col = 4.f, .row = 5.f};
  auto scene = corundum::world::spawn_world(disk(), make_player_config(spawn_dir), registry, tilemap, explicit_pos);
  REQUIRE(scene.has_value());

  CHECK((*scene)->world.transforms.pos_col((*scene)->player) == doctest::Approx(4.f));
  CHECK((*scene)->world.transforms.pos_row((*scene)->player) == doctest::Approx(5.f));
  CHECK((*scene)->zone_id == "village");
}

TEST_CASE("spawn_world — per-map player block wins over the game.json default") {
  const auto dir = temp_dir("spawn_world_per_map");
  const auto registry = make_registry(dir);
  const auto spawn_dir = dir / "spawn_points";
  write_file(spawn_dir / "village.json", R"({"player":{"col":1.5,"row":2.5},"actors":[]})");

  const auto tilemap = make_tilemap(dir / "village.tmx");
  auto scene = corundum::world::spawn_world(disk(), make_player_config(spawn_dir), registry, tilemap);
  REQUIRE(scene.has_value());

  CHECK((*scene)->world.transforms.pos_col((*scene)->player) == doctest::Approx(1.5f));
  CHECK((*scene)->world.transforms.pos_row((*scene)->player) == doctest::Approx(2.5f));
}

TEST_CASE("spawn_world — game.json placement is used when the spawn file has no player block") {
  const auto dir = temp_dir("spawn_world_default");
  const auto registry = make_registry(dir);
  const auto spawn_dir = dir / "spawn_points";
  fs::create_directories(spawn_dir);

  const auto tilemap = make_tilemap(dir / "village.tmx");
  auto scene = corundum::world::spawn_world(disk(), make_player_config(spawn_dir), registry, tilemap);
  REQUIRE(scene.has_value());

  CHECK((*scene)->world.transforms.pos_col((*scene)->player) == doctest::Approx(9.f));
  CHECK((*scene)->world.transforms.pos_row((*scene)->player) == doctest::Approx(9.f));
}

TEST_CASE("spawn_world — file actors spawn at their authored tiles and spawn_file_actors=false skips them") {
  const auto dir = temp_dir("spawn_world_actors");
  const auto registry = make_registry(dir);
  const auto spawn_dir = dir / "spawn_points";
  write_file(spawn_dir / "village.json",
             R"({"actors":[{"id":"npc","col":3,"row":4,"sprite":"hero_walk","facing":"north"}]})");

  const auto tilemap = make_tilemap(dir / "village.tmx");
  auto with_actors = corundum::world::spawn_world(disk(), make_player_config(spawn_dir), registry, tilemap);
  REQUIRE(with_actors.has_value());
  const auto npc = corundum::entities::find_actor((*with_actors)->world, "npc");
  REQUIRE(npc.has_value());
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access): REQUIRE above aborts the case when empty.
  const auto npc_id = npc.value();
  CHECK((*with_actors)->world.transforms.pos_col(npc_id) == doctest::Approx(3.f));
  CHECK((*with_actors)->world.transforms.pos_row(npc_id) == doctest::Approx(4.f));

  auto without_actors =
      corundum::world::spawn_world(disk(), make_player_config(spawn_dir), registry, tilemap, std::nullopt, false);
  REQUIRE(without_actors.has_value());
  CHECK_FALSE(corundum::entities::find_actor((*without_actors)->world, "npc").has_value());
}

TEST_CASE("spawn_world — unknown player walk sprite is an error") {
  const auto dir = temp_dir("spawn_world_bad_sprite");
  const auto registry = make_registry(dir);
  const auto spawn_dir = dir / "spawn_points";
  fs::create_directories(spawn_dir);

  auto cfg = make_player_config(spawn_dir);
  cfg.player.walk_sprite = "missing_walk";
  const auto tilemap = make_tilemap(dir / "village.tmx");
  const auto scene = corundum::world::spawn_world(disk(), cfg, registry, tilemap);
  REQUIRE_FALSE(scene.has_value());
  CHECK(scene.error().find("missing_walk") != std::string::npos);
}

TEST_CASE("sync_chunk_actors — spawns actors for resident chunks") {
  const auto dir = temp_dir("sync_chunk_actors_spawn");
  const auto registry = make_registry(dir);
  const auto spawn_dir = dir / "spawn_points";
  write_file(spawn_dir / "chunk_0_0.json", R"({"actors":[{"id":"c00","col":1,"row":2,"sprite":"hero_walk"}]})");
  write_file(spawn_dir / "chunk_1_0.json", R"({"actors":[{"id":"c10","col":3,"row":4,"sprite":"hero_walk"}]})");

  const auto cfg = make_player_config(spawn_dir);
  const auto render = make_world_render({{.col = 0, .row = 0}, {.col = 1, .row = 0}}, 16);
  corundum::world::Scene scene;

  corundum::world::sync_chunk_actors(disk(), scene, render, cfg, registry);
  CHECK(scene.chunk_actors.size() == 2);
  const auto c00 = corundum::entities::find_actor(scene.world, "c00");
  const auto c10 = corundum::entities::find_actor(scene.world, "c10");
  REQUIRE(c00.has_value());
  REQUIRE(c10.has_value());
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access): REQUIRE above aborts the case when empty.
  const auto c00_id = c00.value();
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access): REQUIRE above aborts the case when empty.
  const auto c10_id = c10.value();
  CHECK(scene.world.transforms.pos_col(c00_id) == doctest::Approx(1.f));
  CHECK(scene.world.transforms.pos_row(c00_id) == doctest::Approx(2.f));
  CHECK(scene.world.transforms.pos_col(c10_id) == doctest::Approx(19.f)); // chunk col 1 at 16 tiles/chunk
  CHECK(scene.world.transforms.pos_row(c10_id) == doctest::Approx(4.f));
}

TEST_CASE("sync_chunk_actors — retains a departed set until flush, then prunes it") {
  const auto dir = temp_dir("sync_chunk_actors_prune");
  const auto registry = make_registry(dir);
  const auto spawn_dir = dir / "spawn_points";
  write_file(spawn_dir / "chunk_0_0.json", R"({"actors":[{"id":"c00","col":1,"row":2,"sprite":"hero_walk"}]})");
  write_file(spawn_dir / "chunk_1_0.json", R"({"actors":[{"id":"c10","col":3,"row":4,"sprite":"hero_walk"}]})");

  const auto cfg = make_player_config(spawn_dir);
  auto render = make_world_render({{.col = 0, .row = 0}, {.col = 1, .row = 0}}, 16);
  auto &world_area = corundum::test::world_area_of(render);
  corundum::world::Scene scene;
  corundum::world::sync_chunk_actors(disk(), scene, render, cfg, registry);
  REQUIRE(scene.chunk_actors.size() == 2);

  // Depart chunk (1,0): the set is retained while its queued deletion is still live.
  world_area.chunks.prune_active([](const corundum::render::ChunkEntry &e) { return e.coord.col == 0; });
  corundum::world::sync_chunk_actors(disk(), scene, render, cfg, registry);
  CHECK(scene.chunk_actors.size() == 2);

  corundum::entities::flush_deletions(scene.world);
  CHECK_FALSE(corundum::entities::find_actor(scene.world, "c10").has_value());

  // The next reconcile prunes the now-empty departed set.
  corundum::world::sync_chunk_actors(disk(), scene, render, cfg, registry);
  CHECK(scene.chunk_actors.size() == 1);
  CHECK(corundum::entities::find_actor(scene.world, "c00").has_value());
}

TEST_CASE("sync_chunk_actors — chunk actor spawn failure rolls back and is not retried") {
  const auto dir = temp_dir("sync_chunk_actors_failure");
  const auto registry = make_registry(dir);
  const auto spawn_dir = dir / "spawn_points";
  write_file(spawn_dir / "chunk_0_0.json", R"({"actors":[{"id":"ok","col":1,"row":1,"sprite":"hero_walk"},)"
                                           R"({"id":"bad","col":2,"row":2,"sprite":"missing"}]})");

  const auto cfg = make_player_config(spawn_dir);
  const auto render = make_world_render({{.col = 0, .row = 0}}, 16);
  corundum::world::Scene scene;

  corundum::world::sync_chunk_actors(disk(), scene, render, cfg, registry);
  REQUIRE(scene.chunk_actors.size() == 1);
  CHECK(scene.chunk_actors.front().load_failed);
  CHECK(scene.world.entities.alive() == 0u); // the already-spawned 'ok' actor was rolled back

  corundum::world::sync_chunk_actors(disk(), scene, render, cfg, registry);
  CHECK(scene.world.entities.alive() == 0u); // load_failed suppresses the retry
}

TEST_CASE("sync_chunk_actors — actors despawned by gameplay stay gone while their chunk is resident") {
  const auto dir = temp_dir("sync_chunk_actors_no_resurrect");
  const auto registry = make_registry(dir);
  const auto spawn_dir = dir / "spawn_points";
  write_file(spawn_dir / "chunk_0_0.json", R"({"actors":[{"id":"c00","col":1,"row":2,"sprite":"hero_walk"}]})");

  const auto cfg = make_player_config(spawn_dir);
  const auto render = make_world_render({{.col = 0, .row = 0}}, 16);
  corundum::world::Scene scene;
  corundum::world::sync_chunk_actors(disk(), scene, render, cfg, registry);
  const auto c00 = corundum::entities::find_actor(scene.world, "c00");
  REQUIRE(c00.has_value());
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access): REQUIRE above aborts the case when empty.
  const auto c00_id = c00.value();

  corundum::entities::mark_for_deletion(scene.world, c00_id);
  corundum::entities::flush_deletions(scene.world);
  corundum::world::sync_chunk_actors(disk(), scene, render, cfg, registry);

  CHECK_FALSE(corundum::entities::find_actor(scene.world, "c00").has_value());
  CHECK(scene.world.entities.alive() == 0u);
}

TEST_CASE("sync_chunk_actors — a resident chunk's spawn file is read once, not every step") {
  const auto dir = temp_dir("sync_chunk_actors_read_once");
  const auto registry = make_registry(dir);
  const auto spawn_dir = dir / "spawn_points";
  fs::create_directories(spawn_dir);

  const auto cfg = make_player_config(spawn_dir);
  const auto render = make_world_render({{.col = 0, .row = 0}}, 16);
  corundum::world::Scene scene;
  corundum::world::sync_chunk_actors(disk(), scene, render, cfg, registry); // no file yet: tracked, empty
  REQUIRE(scene.chunk_actors.size() == 1);

  // A file that appears while the chunk stays resident proves the set is not re-read.
  write_file(spawn_dir / "chunk_0_0.json", R"({"actors":[{"id":"late","col":1,"row":1,"sprite":"hero_walk"}]})");
  corundum::world::sync_chunk_actors(disk(), scene, render, cfg, registry);

  CHECK_FALSE(corundum::entities::find_actor(scene.world, "late").has_value());
}

TEST_CASE("sync_chunk_actors — a departed chunk that returns after the flush respawns its actors") {
  const auto dir = temp_dir("sync_chunk_actors_return");
  const auto registry = make_registry(dir);
  const auto spawn_dir = dir / "spawn_points";
  write_file(spawn_dir / "chunk_0_0.json", R"({"actors":[{"id":"c00","col":1,"row":2,"sprite":"hero_walk"}]})");
  write_file(spawn_dir / "chunk_1_0.json", R"({"actors":[{"id":"c10","col":3,"row":4,"sprite":"hero_walk"}]})");

  const auto cfg = make_player_config(spawn_dir);
  auto render = make_world_render({{.col = 0, .row = 0}, {.col = 1, .row = 0}}, 16);
  auto &world_area = corundum::test::world_area_of(render);
  corundum::world::Scene scene;
  corundum::world::sync_chunk_actors(disk(), scene, render, cfg, registry);

  world_area.chunks.prune_active([](const corundum::render::ChunkEntry &e) { return e.coord.col == 0; });
  corundum::world::sync_chunk_actors(disk(), scene, render, cfg, registry); // queues c10 for deletion
  corundum::entities::flush_deletions(scene.world);
  REQUIRE_FALSE(corundum::entities::find_actor(scene.world, "c10").has_value());

  corundum::render::ChunkEntry returning;
  returning.coord = {.col = 1, .row = 0};
  world_area.chunks.add_active(std::move(returning));
  corundum::world::sync_chunk_actors(disk(), scene, render, cfg, registry);

  CHECK(corundum::entities::find_actor(scene.world, "c10").has_value());
}

TEST_CASE("sync_chunk_actors — no-op outside World mode") {
  const auto dir = temp_dir("sync_chunk_actors_single_map");
  const auto registry = make_registry(dir);
  const auto spawn_dir = dir / "spawn_points";
  write_file(spawn_dir / "chunk_0_0.json", R"({"actors":[{"id":"c00","col":1,"row":2,"sprite":"hero_walk"}]})");

  const auto cfg = make_player_config(spawn_dir);
  auto render = make_world_render({{.col = 0, .row = 0}}, 16);
  render.area.emplace<corundum::render::SingleMapArea>();
  corundum::world::Scene scene;

  corundum::world::sync_chunk_actors(disk(), scene, render, cfg, registry);
  CHECK(scene.chunk_actors.empty());
  CHECK(scene.world.entities.alive() == 0u);
}
