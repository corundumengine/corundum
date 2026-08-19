#include <doctest/doctest.h>

#include <corundum/core/game_config.hpp>
#include <corundum/engine.hpp>
#include <corundum/platform/null/null_platform.hpp>

#include <expected>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

  /// Build a GameConfig pointing at the headless fixtures bundled with the
  /// test target. sprites_dir ends at the parent directory so that
  /// `<sprites_dir>/characters/<sheet>.json` resolves correctly.
  corundum::core::GameConfig make_fixture_config(const fs::path &fixtures_root) {
    corundum::core::GameConfig cfg{};
    cfg.window_title = "lifecycle_test";
    cfg.win_w = 320.f;
    cfg.win_h = 240.f;
    cfg.paths.sprites_dir = (fixtures_root / "sprites").string();
    cfg.paths.tilemap_path = (fixtures_root / "tilemaps/lifecycle_test.json").string();
    cfg.paths.font_dir = (fixtures_root / "fonts").string();
    cfg.paths.game_font = "missing.ttf"; // NullRenderer ignores file existence
    cfg.paths.world_manifest_path.clear();
    cfg.paths.spawn_points_dir = (fixtures_root / "spawn_points").string();
    cfg.paths.portals_dir = (fixtures_root / "portals").string();
    cfg.paths.dialogue_dir.clear();
    cfg.paths.quests_dir.clear();
    cfg.paths.sounds_dir.clear();
    return cfg;
  }

  /// Adopt the NullPlatform into @p engine and reset engine state between tests.
  void adopt_platform(corundum::Engine &engine, unsigned w, unsigned h) {
    corundum::platform::null::NullPlatform platform = corundum::platform::null::make_null_platform(w, h);
    corundum::platform::null::adopt_null_platform(engine, platform);
  }

} // namespace

// ── 1. Precondition guard ────────────────────────────────────────────────────

TEST_CASE("lifecycle: initialize on a default-constructed Engine returns an error") {
  corundum::Engine engine{};
  std::expected<void, std::string> result = corundum::initialize(engine, corundum::core::GameConfig{});
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().find("must be non-null") != std::string::npos);
}

// ── 2. Initialize succeeds with NullPlatform + fixture GameConfig ───────────

TEST_CASE("lifecycle: initialize succeeds with NullPlatform and a fixture GameConfig") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(fs::is_directory(fixtures));

  corundum::core::GameConfig cfg = make_fixture_config(fixtures);
  std::expected<void, std::string> result = corundum::initialize(engine, std::move(cfg));
  REQUIRE(result.has_value());
  CHECK(engine.window->is_open());
  corundum::cleanup(engine);
  CHECK_FALSE(engine.window->is_open());
}

// ── 3. Failure path runs cleanup (window closes, error returned) ────────────

TEST_CASE("lifecycle: initialize failure runs cleanup so the window is closed") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  corundum::core::GameConfig cfg = make_fixture_config(fixtures);
  cfg.paths.sprites_dir = (fixtures / "no_such_sprites_dir").string();

  std::expected<void, std::string> result = corundum::initialize(engine, std::move(cfg));
  REQUIRE_FALSE(result.has_value());
  CHECK_FALSE(engine.window->is_open());
}

// ── 4. Double cleanup safe; run_frame/run_loop after cleanup return false ────

TEST_CASE("lifecycle: cleanup is idempotent and post-cleanup run_frame returns false") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  corundum::core::GameConfig cfg = make_fixture_config(fixtures);
  REQUIRE(corundum::initialize(engine, std::move(cfg)).has_value());

  corundum::cleanup(engine);
  // Second cleanup must not crash.
  corundum::cleanup(engine);

  // Both run_frame and run_loop must exit immediately.
  CHECK_FALSE(corundum::run_frame(engine));
  corundum::run_loop(engine);
  CHECK(engine.quit);
}

// ── 5. on_fixed_update can request_quit, run_loop exits cleanly ─────────────

TEST_CASE("lifecycle: on_fixed_update calling request_quit ends the main loop") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  corundum::core::GameConfig cfg = make_fixture_config(fixtures);
  REQUIRE(corundum::initialize(engine, std::move(cfg)).has_value());

  int hook_calls = 0;
  engine.on_fixed_update = [&hook_calls](corundum::Engine &e, float) {
    ++hook_calls;
    corundum::request_quit(e);
  };

  // Force at least one fixed step on the next tick.
  constexpr float k_steps_to_accumulate = 2.f;
  engine.timer.accumulator = engine.timer.target_dt * k_steps_to_accumulate;

  corundum::run_loop(engine);

  CHECK(hook_calls >= 1);
  CHECK(engine.quit);
  // Window should still be open after run_loop (single-close-path contract);
  // cleanup closes it.
  CHECK(engine.window->is_open());
  corundum::cleanup(engine);
  CHECK_FALSE(engine.window->is_open());
}

// ── 6. run_frame returns true across N steps, then false after request_quit ──

TEST_CASE("lifecycle: run_frame steps the simulation N times then returns false") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  corundum::core::GameConfig cfg = make_fixture_config(fixtures);
  REQUIRE(corundum::initialize(engine, std::move(cfg)).has_value());

  constexpr float k_steps_to_accumulate = 2.f;
  constexpr int k_frames_to_pump = 5;

  // Pump frames; each should succeed.
  for (int i = 0; i < k_frames_to_pump; ++i) {
    engine.timer.accumulator = engine.timer.target_dt * k_steps_to_accumulate;
    CHECK(corundum::run_frame(engine));
  }

  // request_quit flips engine.quit; the next call must return false.
  corundum::request_quit(engine);
  CHECK_FALSE(corundum::run_frame(engine));

  corundum::cleanup(engine);
}
