#include <doctest/doctest.h>

#include <corundum/core/game_config.hpp>
#include <corundum/debug/debug_overlay.hpp>
#include <corundum/engine.hpp>
#include <corundum/platform/null/null_platform.hpp>

#include <cmath>
#include <expected>
#include <filesystem>
#include <string>
#include <type_traits>
#include <utility>

namespace fs = std::filesystem;

namespace {

  /// Bundle the lifecycle setup used by every case here so each TEST_CASE reads
  /// as a single focused scenario.
  corundum::Engine make_initialised_engine(const fs::path &fixtures) {
    corundum::Engine engine{};
    corundum::platform::null::NullPlatform platform = corundum::platform::null::make_null_platform(320, 240);
    corundum::platform::null::adopt_null_platform(engine, platform);

    corundum::core::GameConfig cfg{};
    cfg.window_title = "hud_overlay_test";
    cfg.win_w = 320.f;
    cfg.win_h = 240.f;
    cfg.paths.sprites_dir = (fixtures / "sprites").string();
    cfg.paths.tilemap_path = (fixtures / "tilemaps/lifecycle_test.json").string();
    cfg.paths.font_dir = (fixtures / "fonts").string();
    cfg.paths.game_font = "missing.ttf"; // NullRenderer ignores file existence
    cfg.paths.world_manifest_path.clear();
    cfg.paths.spawn_points_dir = (fixtures / "spawn_points").string();
    cfg.paths.portals_dir = (fixtures / "portals").string();
    cfg.paths.dialogue_dir.clear();
    cfg.paths.quests_dir.clear();
    cfg.paths.sounds_dir.clear();

    const auto result = corundum::initialize(engine, std::move(cfg));
    if (!result)
      throw std::runtime_error(std::string{"hud_overlay_test setup failed: "} + result.error());
    return engine;
  }

} // namespace

TEST_CASE("HudOverlay: default construction leaves the overlay disabled with zero FPS") {
  corundum::debug::HudOverlay overlay;
  CHECK_FALSE(overlay.enabled);
  CHECK(overlay.smoothed_fps == 0.f);
}

TEST_CASE("HudOverlay: movable but non-copyable") {
  corundum::debug::HudOverlay src;
  src.enabled = true;
  src.smoothed_fps = 42.f;
  const corundum::debug::HudOverlay dst = std::move(src);
  CHECK(dst.enabled);
  CHECK(dst.smoothed_fps == 42.f);

  static_assert(!std::is_copy_constructible_v<corundum::debug::HudOverlay>);
  static_assert(!std::is_copy_assignable_v<corundum::debug::HudOverlay>);
  static_assert(std::is_nothrow_move_constructible_v<corundum::debug::HudOverlay>);
  static_assert(std::is_nothrow_move_assignable_v<corundum::debug::HudOverlay>);
}

TEST_CASE("HudOverlay::render on NullRenderer draws without crashing and updates the FPS EMA") {
  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(fs::is_directory(fixtures));

  corundum::Engine engine = make_initialised_engine(fixtures);

  // Pretend a frame just ran with last_frame_dt = 1/60 → raw_fps = 60.
  engine.timer.last_frame_dt = 1.f / 60.f;
  engine.hud.enabled = true;
  engine.hud.smoothed_fps = 0.f;

  const corundum::debug::OverlayInput input{
      .render_state = engine.render,
      .cfg = engine.cfg,
      .scene = engine.scene,
      .timer = engine.timer,
  };

  engine.hud.render(*engine.renderer, input);

  // First-tick EMA on 60 Hz should sit at alpha * 60 = 0.05 * 60 = 3.0.
  CHECK(engine.hud.smoothed_fps == doctest::Approx(3.0f).epsilon(1e-4f));

  corundum::cleanup(engine);
}

TEST_CASE("HudOverlay::render is a no-op on the renderer when smoothed_fps starts non-zero") {
  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(fs::is_directory(fixtures));

  corundum::Engine engine = make_initialised_engine(fixtures);

  engine.timer.last_frame_dt = 1.f / 60.f;
  engine.hud.enabled = true;
  engine.hud.smoothed_fps = 50.f; // pre-existing EMA value

  const corundum::debug::OverlayInput input{
      .render_state = engine.render,
      .cfg = engine.cfg,
      .scene = engine.scene,
      .timer = engine.timer,
  };

  engine.hud.render(*engine.renderer, input);

  // EMA: 50 + 0.05 * (60 - 50) = 50.5.
  CHECK(engine.hud.smoothed_fps == doctest::Approx(50.5f).epsilon(1e-4f));

  corundum::cleanup(engine);
}

TEST_CASE("HudOverlay::render handles a zero last_frame_dt without dividing by zero") {
  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(fs::is_directory(fixtures));

  corundum::Engine engine = make_initialised_engine(fixtures);

  engine.timer.last_frame_dt = 0.f;
  engine.hud.enabled = true;
  engine.hud.smoothed_fps = 30.f;

  const corundum::debug::OverlayInput input{
      .render_state = engine.render,
      .cfg = engine.cfg,
      .scene = engine.scene,
      .timer = engine.timer,
  };

  engine.hud.render(*engine.renderer, input);

  // raw_fps clamps to 0 when last_frame_dt is 0, so the EMA pulls toward zero:
  // 30 + 0.05 * (0 - 30) = 30 - 1.5 = 28.5. No division-by-zero, no NaN.
  CHECK(engine.hud.smoothed_fps == doctest::Approx(28.5f).epsilon(1e-6f));
  CHECK_FALSE(std::isnan(engine.hud.smoothed_fps));

  corundum::cleanup(engine);
}
