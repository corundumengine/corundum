// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include "temp_dir.hpp"

#include <corundum/core/game_config.hpp>
#include <corundum/core/window_mode.hpp>

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
    return corundum::test::TempDir{"crpg_test_game_config_", tag};
  }

} // namespace

using corundum::core::load_game_config;

// ── File errors ──────────────────────────────────────────────────────────────

TEST_CASE("load_game_config — missing file returns error") {
  const auto result = load_game_config("/nonexistent/path/game.json");
  CHECK(!result.has_value());
}

TEST_CASE("load_game_config — malformed JSON returns error") {
  const auto dir = temp_dir("malformed");
  const auto p = dir / "game.json";
  write_file(p, "{bad json");
  const auto result = load_game_config(p);
  CHECK(!result.has_value());
}

TEST_CASE("load_game_config — // and /* */ comments are ignored") {
  const auto dir = temp_dir("comments");
  const auto p = dir / "game.json";
  write_file(p, R"({
    // window setup
    "window_title": "Commented", /* inline */
    "simulation_fps": 30
  })");
  const auto result = load_game_config(p);
  REQUIRE(result.has_value());
  CHECK(result->window_title == "Commented");
  CHECK(result->simulation_fps == 30);
}

TEST_CASE("load_game_config — JSON array (not object) returns error") {
  const auto dir = temp_dir("array");
  const auto p = dir / "game.json";
  write_file(p, "[]");
  const auto result = load_game_config(p);
  CHECK(!result.has_value());
}

TEST_CASE("load_game_config — full valid JSON loads all fields") {
  const auto dir = temp_dir("full");
  const auto p = dir / "game.json";
  write_file(p, R"({
        "win_w": 1280.0, "win_h": 720.0, "simulation_fps": 30,
        "interact_radius": 64.0, "player_speed": 150.0,
        "character_scale": 3, "tile_scale": 4, "elevation_step_px": 6.0,
        "font_dir": "game/assets/fonts", "game_font": "MyFont.ttf",
        "tilemap_path": "data/tilemaps/dungeon.json",
        "sprites_dir": "data/sprite_sheets/dungeon.json",
        "dialogue_dir": "data/npc",
        "dialogue_render": {
            "font_size_speaker": 30, "font_size_body": 24, "font_size_prompt": 16,
            "margin": 15.0, "line_spacing": 28.0, "panel_height_frac": 0.4
        }
    })");
  const auto result = load_game_config(p);
  REQUIRE(result.has_value());
  const auto &cfg = *result;
  CHECK(cfg.win_w == doctest::Approx(1280.f));
  CHECK(cfg.win_h == doctest::Approx(720.f));
  CHECK(cfg.simulation_fps == 30u);
  CHECK(cfg.interact_radius == doctest::Approx(64.f));
  CHECK(cfg.player_speed == doctest::Approx(150.f));
  CHECK(cfg.character_scale == doctest::Approx(3.f));
  CHECK(cfg.tile_scale == doctest::Approx(4.f));
  CHECK(cfg.elevation_step_px == doctest::Approx(6.f));
  CHECK(cfg.paths.font_dir == "game/assets/fonts");
  CHECK(cfg.paths.game_font == "MyFont.ttf");
  CHECK(cfg.paths.tilemap_path == "data/tilemaps/dungeon.json");
  CHECK(cfg.paths.sprites_dir == "data/sprite_sheets/dungeon.json");
  CHECK(cfg.paths.dialogue_dir == "data/npc");
  CHECK(cfg.dialogue_render.font_size_speaker == 30u);
  CHECK(cfg.dialogue_render.font_size_body == 24u);
  CHECK(cfg.dialogue_render.font_size_prompt == 16u);
  CHECK(cfg.dialogue_render.margin == doctest::Approx(15.f));
  CHECK(cfg.dialogue_render.line_spacing == doctest::Approx(28.f));
  CHECK(cfg.dialogue_render.panel_height_frac == doctest::Approx(0.4f));
}

// ── Partial JSON ─────────────────────────────────────────────────────────────

TEST_CASE("load_game_config — partial JSON overrides only specified fields") {
  const auto dir = temp_dir("partial");
  const auto p = dir / "game.json";
  write_file(p, R"({"win_w": 1024.0, "player_speed": 300.0})");
  const auto result = load_game_config(p);
  REQUIRE(result.has_value());
  const auto &cfg = *result;
  CHECK(cfg.win_w == doctest::Approx(1024.f));
  CHECK(cfg.win_h == doctest::Approx(600.f)); // default
  CHECK(cfg.player_speed == doctest::Approx(300.f));
  CHECK(cfg.character_scale == doctest::Approx(2.f));   // default
  CHECK(cfg.elevation_step_px == doctest::Approx(4.f)); // default
}

// ── Numeric validation ────────────────────────────────────────────────────────

TEST_CASE("load_game_config — win_w <= 0 returns error") {
  const auto dir = temp_dir("win_w_zero");
  const auto p = dir / "game.json";
  write_file(p, R"({"win_w": 0.0})");
  const auto result = load_game_config(p);
  CHECK(!result.has_value());
}

TEST_CASE("load_game_config — win_h <= 0 returns error") {
  const auto dir = temp_dir("win_h_neg");
  const auto p = dir / "game.json";
  write_file(p, R"({"win_h": -1.0})");
  const auto result = load_game_config(p);
  CHECK(!result.has_value());
}

TEST_CASE("load_game_config — simulation_fps = 0 returns error") {
  const auto dir = temp_dir("simulation_fps_zero");
  const auto p = dir / "game.json";
  write_file(p, R"({"simulation_fps": 0})");
  const auto result = load_game_config(p);
  CHECK(!result.has_value());
}

TEST_CASE("load_game_config — simulation_fps above the supported maximum returns error") {
  const auto dir = temp_dir("simulation_fps_too_large");
  const auto p = dir / "game.json";
  write_file(p, R"({"simulation_fps": 100000})");
  const auto result = load_game_config(p);
  CHECK(!result.has_value());
}

TEST_CASE("load_game_config — player_speed <= 0 returns error") {
  const auto dir = temp_dir("player_speed_zero");
  const auto p = dir / "game.json";
  write_file(p, R"({"player_speed": 0.0})");
  const auto result = load_game_config(p);
  CHECK(!result.has_value());
}

TEST_CASE("load_game_config — character_scale == 0 returns error") {
  const auto dir = temp_dir("character_scale_zero");
  const auto p = dir / "game.json";
  write_file(p, R"({"character_scale": 0})");
  const auto result = load_game_config(p);
  CHECK(!result.has_value());
}

TEST_CASE("load_game_config — tile_scale == 0 returns error") {
  const auto dir = temp_dir("tile_scale_zero");
  const auto p = dir / "game.json";
  write_file(p, R"({"tile_scale": 0})");
  const auto result = load_game_config(p);
  CHECK(!result.has_value());
}

TEST_CASE("load_game_config — character_scale negative returns error") {
  const auto dir = temp_dir("character_scale_neg");
  const auto p = dir / "game.json";
  write_file(p, R"({"character_scale": -1})");
  const auto result = load_game_config(p);
  CHECK(!result.has_value());
}

TEST_CASE("load_game_config — interact_radius <= 0 returns error") {
  const auto dir = temp_dir("interact_radius_zero");
  const auto p = dir / "game.json";
  write_file(p, R"({"interact_radius": 0.0})");
  const auto result = load_game_config(p);
  CHECK(!result.has_value());
}

TEST_CASE("load_game_config — elevation_step_px <= 0 returns error") {
  const auto dir = temp_dir("elevation_step_px_zero");
  const auto p = dir / "game.json";
  write_file(p, R"({"elevation_step_px": 0.0})");
  const auto result = load_game_config(p);
  CHECK(!result.has_value());
}

TEST_CASE("load_game_config — min_zoom > max_zoom returns error") {
  const auto dir = temp_dir("zoom_out_of_order");
  const auto p = dir / "game.json";
  write_file(p, R"({"min_zoom": 4.0, "max_zoom": 2.0})");
  const auto result = load_game_config(p);
  CHECK(!result.has_value());
}

TEST_CASE("load_game_config — min_zoom == max_zoom is allowed") {
  const auto dir = temp_dir("zoom_equal");
  const auto p = dir / "game.json";
  write_file(p, R"({"min_zoom": 2.0, "max_zoom": 2.0})");
  const auto result = load_game_config(p);
  REQUIRE(result.has_value());
  CHECK(result->min_zoom == doctest::Approx(2.0));
  CHECK(result->max_zoom == doctest::Approx(2.0));
}

// ── String validation ─────────────────────────────────────────────────────────

TEST_CASE("load_game_config — empty game_font returns error") {
  const auto dir = temp_dir("empty_game_font");
  const auto p = dir / "game.json";
  write_file(p, R"({"game_font": ""})");
  const auto result = load_game_config(p);
  CHECK(!result.has_value());
}

TEST_CASE("load_game_config — empty font_dir returns error") {
  const auto dir = temp_dir("empty_font_dir");
  const auto p = dir / "game.json";
  write_file(p, R"({"font_dir": ""})");
  const auto result = load_game_config(p);
  CHECK(!result.has_value());
}

TEST_CASE("load_game_config — empty tilemap_path returns error") {
  const auto dir = temp_dir("empty_tilemap_path");
  const auto p = dir / "game.json";
  write_file(p, R"({"tilemap_path": ""})");
  const auto result = load_game_config(p);
  CHECK(!result.has_value());
}

TEST_CASE("load_game_config — empty sprites_dir returns error") {
  const auto dir = temp_dir("empty_sprites_dir");
  const auto p = dir / "game.json";
  write_file(p, R"({"sprites_dir": ""})");
  const auto result = load_game_config(p);
  CHECK(!result.has_value());
}

TEST_CASE("load_game_config — empty dialogue_dir returns error") {
  const auto dir = temp_dir("empty_dialogue_dir");
  const auto p = dir / "game.json";
  write_file(p, R"({"dialogue_dir": ""})");
  const auto result = load_game_config(p);
  CHECK(!result.has_value());
}

// ── panel_height_frac ─────────────────────────────────────────────────────────

TEST_CASE("load_game_config — panel_height_frac = 0 returns error") {
  const auto dir = temp_dir("phf_zero");
  const auto p = dir / "game.json";
  write_file(p, R"({"dialogue_render": {"panel_height_frac": 0.0}})");
  const auto result = load_game_config(p);
  CHECK(!result.has_value());
}

TEST_CASE("load_game_config — panel_height_frac = 1 returns error") {
  const auto dir = temp_dir("phf_one");
  const auto p = dir / "game.json";
  write_file(p, R"({"dialogue_render": {"panel_height_frac": 1.0}})");
  const auto result = load_game_config(p);
  CHECK(!result.has_value());
}

TEST_CASE("load_game_config — panel_height_frac > 1 returns error") {
  const auto dir = temp_dir("phf_over");
  const auto p = dir / "game.json";
  write_file(p, R"({"dialogue_render": {"panel_height_frac": 1.5}})");
  const auto result = load_game_config(p);
  CHECK(!result.has_value());
}

// ── dialogue_render sub-object ────────────────────────────────────────────────

TEST_CASE("load_game_config — absent dialogue_render uses defaults") {
  const auto dir = temp_dir("no_dr");
  const auto p = dir / "game.json";
  write_file(p, "{}");
  const auto result = load_game_config(p);
  REQUIRE(result.has_value());
  const auto &cfg = *result;
  CHECK(cfg.dialogue_render.font_size_body == 22u);
  CHECK(cfg.dialogue_render.panel_height_frac == doctest::Approx(0.32f));
}

TEST_CASE("load_game_config — partial dialogue_render merges with defaults") {
  const auto dir = temp_dir("partial_dr");
  const auto p = dir / "game.json";
  write_file(p, R"({"dialogue_render": {"font_size_body": 28}})");
  const auto result = load_game_config(p);
  REQUIRE(result.has_value());
  const auto &cfg = *result;
  CHECK(cfg.dialogue_render.font_size_body == 28u);
  CHECK(cfg.dialogue_render.font_size_speaker == 26u); // default
  CHECK(cfg.dialogue_render.font_size_prompt == 18u);  // default
}

TEST_CASE("load_game_config — dialogue_render not an object returns error") {
  const auto dir = temp_dir("dr_not_obj");
  const auto p = dir / "game.json";
  write_file(p, R"({"dialogue_render": 42})");
  const auto result = load_game_config(p);
  CHECK(!result.has_value());
}

// ── Default paths ──────────────────────────────────────────────────────────────

TEST_CASE("load_game_config — quests_dir defaults to data/quests when absent") {
  const auto dir = temp_dir("default_quests");
  const auto p = dir / "game.json";
  write_file(p, "{}");
  const auto result = load_game_config(p);
  REQUIRE(result.has_value());
  const auto &cfg = *result;
  CHECK(cfg.paths.quests_dir == "data/quests");
}

TEST_CASE("load_game_config — resource directories default to the standard project layout") {
  const auto dir = temp_dir("default_resource_dirs");
  const auto p = dir / "game.json";
  write_file(p, "{}");
  const auto result = load_game_config(p);
  REQUIRE(result.has_value());
  const auto &cfg = *result;
  CHECK(cfg.paths.font_dir == "assets/fonts");
  CHECK(cfg.paths.sprites_dir == "data/sprite_sheets");
  CHECK(cfg.paths.spawn_points_dir == "data/spawn_points");
  CHECK(cfg.paths.portals_dir == "data/portals");
  CHECK(cfg.paths.dialogue_dir == "data/dialogue");
}

// ── Forward compatibility ─────────────────────────────────────────────────────

TEST_CASE("load_game_config — unknown top-level key is silently ignored") {
  const auto dir = temp_dir("unknown_key");
  const auto p = dir / "game.json";
  write_file(p, R"({"win_w": 1024.0, "future_feature": true})");
  const auto result = load_game_config(p);
  REQUIRE(result.has_value());
  const auto &cfg = *result;
  CHECK(cfg.win_w == doctest::Approx(1024.f));
}

// ── Player block ──────────────────────────────────────────────────────────────

TEST_CASE("load_game_config — absent player block uses defaults") {
  const auto dir = temp_dir("no_player");
  const auto p = dir / "game.json";
  write_file(p, "{}");
  const auto result = load_game_config(p);
  REQUIRE(result.has_value());
  const auto &cfg = *result;
  CHECK(cfg.player.walk_sprite == "player_walk");
  CHECK(cfg.player.idle_sprite == "player_idle");
  CHECK(cfg.player.col == doctest::Approx(8.f));
  CHECK(cfg.player.row == doctest::Approx(8.f));
}

TEST_CASE("load_game_config — full player block parsed") {
  const auto dir = temp_dir("full_player");
  const auto p = dir / "game.json";
  write_file(p, R"({
    "player": {
      "walk_sprite": "hero_walk",
      "idle_sprite": "hero_idle",
      "col": 12.5,
      "row": 3.0
    }
  })");
  const auto result = load_game_config(p);
  REQUIRE(result.has_value());
  const auto &cfg = *result;
  CHECK(cfg.player.walk_sprite == "hero_walk");
  CHECK(cfg.player.idle_sprite == "hero_idle");
  CHECK(cfg.player.col == doctest::Approx(12.5f));
  CHECK(cfg.player.row == doctest::Approx(3.f));
}

TEST_CASE("load_game_config — partial player block merges with defaults") {
  const auto dir = temp_dir("partial_player");
  const auto p = dir / "game.json";
  write_file(p, R"({
    "player": { "walk_sprite": "hero_walk" }
  })");
  const auto result = load_game_config(p);
  REQUIRE(result.has_value());
  const auto &cfg = *result;
  CHECK(cfg.player.walk_sprite == "hero_walk");
  CHECK(cfg.player.idle_sprite == "player_idle"); // default
  CHECK(cfg.player.col == doctest::Approx(8.f));  // default
  CHECK(cfg.player.row == doctest::Approx(8.f));  // default
}

TEST_CASE("load_game_config — player not an object returns error") {
  const auto dir = temp_dir("player_not_obj");
  const auto p = dir / "game.json";
  write_file(p, R"({"player": 42})");
  const auto result = load_game_config(p);
  CHECK(!result.has_value());
}

TEST_CASE("load_game_config — player empty walk_sprite returns error") {
  const auto dir = temp_dir("player_empty_walk");
  const auto p = dir / "game.json";
  write_file(p, R"({"player": {"walk_sprite": ""}})");
  const auto result = load_game_config(p);
  CHECK(!result.has_value());
}

TEST_CASE("load_game_config — player empty idle_sprite returns error") {
  const auto dir = temp_dir("player_empty_idle");
  const auto p = dir / "game.json";
  write_file(p, R"({"player": {"idle_sprite": ""}})");
  const auto result = load_game_config(p);
  CHECK(!result.has_value());
}

TEST_CASE("load_game_config — player negative col returns error") {
  const auto dir = temp_dir("player_neg_col");
  const auto p = dir / "game.json";
  write_file(p, R"({"player": {"col": -1.0}})");
  const auto result = load_game_config(p);
  CHECK(!result.has_value());
}

TEST_CASE("load_game_config — player negative row returns error") {
  const auto dir = temp_dir("player_neg_row");
  const auto p = dir / "game.json";
  write_file(p, R"({"player": {"row": -0.5}})");
  const auto result = load_game_config(p);
  CHECK(!result.has_value());
}

TEST_CASE("load_game_config — player col wrong type returns error") {
  const auto dir = temp_dir("player_col_type");
  const auto p = dir / "game.json";
  write_file(p, R"({"player": {"col": "abc"}})");
  const auto result = load_game_config(p);
  CHECK(!result.has_value());
}

// ── window_mode ───────────────────────────────────────────────────────────────

TEST_CASE("load_game_config — window_mode fullscreen parses") {
  const auto dir = temp_dir("window_mode_fullscreen");
  const auto p = dir / "game.json";
  write_file(p, R"({"window_mode": "fullscreen"})");
  const auto result = load_game_config(p);
  REQUIRE(result.has_value());
  CHECK(result->window_mode == corundum::core::WindowMode::Fullscreen);
}

TEST_CASE("load_game_config — absent window_mode defaults to windowed") {
  const auto dir = temp_dir("window_mode_absent");
  const auto p = dir / "game.json";
  write_file(p, "{}");
  const auto result = load_game_config(p);
  REQUIRE(result.has_value());
  CHECK(result->window_mode == corundum::core::WindowMode::Windowed);
}

TEST_CASE("load_game_config — unknown window_mode returns error") {
  const auto dir = temp_dir("window_mode_unknown");
  const auto p = dir / "game.json";
  write_file(p, R"({"window_mode": "borderless"})");
  const auto result = load_game_config(p);
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().find("window_mode") != std::string::npos);
}

TEST_CASE("load_game_config — non-string window_mode returns error") {
  const auto dir = temp_dir("window_mode_type");
  const auto p = dir / "game.json";
  write_file(p, R"({"window_mode": true})");
  const auto result = load_game_config(p);
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().find("window_mode") != std::string::npos);
}

// ── rng_seed ──────────────────────────────────────────────────────────────────

TEST_CASE("load_game_config — rng_seed parses as an unsigned integer") {
  const auto dir = temp_dir("rng_seed");
  const auto p = dir / "game.json";
  write_file(p, R"({"rng_seed": 42})");
  const auto result = load_game_config(p);
  REQUIRE(result.has_value());
  REQUIRE(result->rng_seed.has_value());
  CHECK(result->rng_seed == 42u);
}

TEST_CASE("load_game_config — absent rng_seed leaves the seed unset") {
  const auto dir = temp_dir("rng_seed_absent");
  const auto p = dir / "game.json";
  write_file(p, "{}");
  const auto result = load_game_config(p);
  REQUIRE(result.has_value());
  CHECK_FALSE(result->rng_seed.has_value());
}

TEST_CASE("load_game_config — non-integer rng_seed returns an error") {
  const auto dir = temp_dir("rng_seed_type");
  const auto p = dir / "game.json";
  write_file(p, R"({"rng_seed": "forty-two"})");
  const auto result = load_game_config(p);
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().find("rng_seed") != std::string::npos);
}

// ── starting_flags ────────────────────────────────────────────────────────────

TEST_CASE("load_game_config — starting_flags parses integer values") {
  const auto dir = temp_dir("starting_flags");
  const auto p = dir / "game.json";
  write_file(p, R"({"starting_flags": {"gold": 50, "rep.village": -2, "intro_seen": 1}})");
  const auto result = load_game_config(p);
  REQUIRE(result.has_value());
  REQUIRE(result->starting_flags.size() == 3);
  CHECK(result->starting_flags.at("gold") == 50);
  CHECK(result->starting_flags.at("rep.village") == -2);
  CHECK(result->starting_flags.at("intro_seen") == 1);
}

TEST_CASE("load_game_config — absent starting_flags leaves the map empty") {
  const auto dir = temp_dir("starting_flags_absent");
  const auto p = dir / "game.json";
  write_file(p, "{}");
  const auto result = load_game_config(p);
  REQUIRE(result.has_value());
  CHECK(result->starting_flags.empty());
}

TEST_CASE("load_game_config — non-object starting_flags returns an error") {
  const auto dir = temp_dir("starting_flags_not_obj");
  const auto p = dir / "game.json";
  write_file(p, R"({"starting_flags": [1, 2]})");
  const auto result = load_game_config(p);
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().find("starting_flags") != std::string::npos);
}

TEST_CASE("load_game_config — non-integer starting_flags value returns an error") {
  const auto dir = temp_dir("starting_flags_value_type");
  const auto p = dir / "game.json";
  write_file(p, R"({"starting_flags": {"gold": "lots"}})");
  const auto result = load_game_config(p);
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().find("starting_flags.gold") != std::string::npos);
}
