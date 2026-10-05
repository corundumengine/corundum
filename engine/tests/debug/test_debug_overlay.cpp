// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include "font_fixtures.hpp"

#include <corundum/core/game_config.hpp>
#include <corundum/core/math/vec.hpp>
#include <corundum/debug/debug_overlay.hpp>
#include <corundum/engine.hpp>
#include <corundum/platform/null/null_platform.hpp>
#include <corundum/platform/renderer.hpp>

#include <cmath>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

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
    corundum::test::set_missing_fonts(cfg.paths);
    cfg.paths.world_manifest_path.clear();
    cfg.paths.spawn_points_dir = (fixtures / "spawn_points").string();
    cfg.paths.portals_dir = (fixtures / "portals").string();
    cfg.paths.dialogue_dir.clear();
    cfg.paths.quests_dir.clear();
    cfg.paths.sounds_dir.clear();

    const auto result = engine.initialize(std::move(cfg));
    if (!result)
      throw std::runtime_error(std::string{"hud_overlay_test setup failed: "} + result.error());
    return engine;
  }

  /// Renderer double that records the viewport each world view is set with, so a test can prove the
  /// overlay projects world geometry with the live viewport rather than the configured window size.
  class ViewportRecordingRenderer final : public corundum::platform::Renderer {
  public:
    std::vector<corundum::core::math::Vec2> viewports{};

    [[nodiscard]] std::expected<uint32_t, std::string> load_texture(std::string_view /*path*/) override {
      return 1u;
    }

    [[nodiscard]] std::expected<uint32_t, std::string> load_font(std::string_view /*path*/) override {
      return 1u;
    }

    void set_world_view(corundum::core::math::Vec2 /*top_left*/, corundum::core::math::Vec2 viewport_size,
                        float /*zoom*/) override {
      viewports.push_back(viewport_size);
    }

    void reset_screen_view() override {}

    [[nodiscard]] bool begin_frame(corundum::core::math::Colour /*clear_colour*/) override {
      return true;
    }

    void end_frame() override {}

    void draw(const corundum::platform::DrawSprite & /*cmd*/) override {}

    void draw(const corundum::platform::DrawText & /*cmd*/) override {}

    void draw(const corundum::platform::DrawRect & /*cmd*/) override {}

    void draw(const corundum::platform::DrawLine & /*cmd*/) override {}

    [[nodiscard]] float measure_text(uint32_t /*font_id*/, std::string_view text,
                                     uint32_t /*char_size*/) const override {
      return static_cast<float>(text.size()) * 8.f;
    }

    [[nodiscard]] corundum::platform::RendererStats stats() const override {
      return {};
    }
  };

} // namespace

TEST_CASE("HudOverlay: default construction leaves the overlay disabled with zero FPS") {
  const corundum::debug::HudOverlay overlay;
  CHECK_FALSE(overlay.enabled);
  CHECK(overlay.smoothed_fps == 0.f);
  CHECK(overlay.shed_frames == 0u);
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
      .render_state = &engine.render,
      .cfg = &engine.cfg,
      .scene = &engine.scene,
      .timer = &engine.timer,
      .viewport = {.x = 320.f, .y = 240.f},
  };

  engine.hud.render(*engine.renderer, input);

  // First-tick EMA on 60 Hz should sit at alpha * 60 = 0.05 * 60 = 3.0.
  CHECK(engine.hud.smoothed_fps == doctest::Approx(3.0f).epsilon(1e-4f));

  engine.cleanup();
}

TEST_CASE("HudOverlay::render is a no-op on the renderer when smoothed_fps starts non-zero") {
  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(fs::is_directory(fixtures));

  corundum::Engine engine = make_initialised_engine(fixtures);

  engine.timer.last_frame_dt = 1.f / 60.f;
  engine.hud.enabled = true;
  engine.hud.smoothed_fps = 50.f; // pre-existing EMA value

  const corundum::debug::OverlayInput input{
      .render_state = &engine.render,
      .cfg = &engine.cfg,
      .scene = &engine.scene,
      .timer = &engine.timer,
      .viewport = {.x = 320.f, .y = 240.f},
  };

  engine.hud.render(*engine.renderer, input);

  // EMA: 50 + 0.05 * (60 - 50) = 50.5.
  CHECK(engine.hud.smoothed_fps == doctest::Approx(50.5f).epsilon(1e-4f));

  engine.cleanup();
}

TEST_CASE("HudOverlay::render handles a zero last_frame_dt without dividing by zero") {
  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(fs::is_directory(fixtures));

  corundum::Engine engine = make_initialised_engine(fixtures);

  engine.timer.last_frame_dt = 0.f;
  engine.hud.enabled = true;
  engine.hud.smoothed_fps = 30.f;

  const corundum::debug::OverlayInput input{
      .render_state = &engine.render,
      .cfg = &engine.cfg,
      .scene = &engine.scene,
      .timer = &engine.timer,
      .viewport = {.x = 320.f, .y = 240.f},
  };

  engine.hud.render(*engine.renderer, input);

  // raw_fps clamps to 0 when last_frame_dt is 0, so the EMA pulls toward zero:
  // 30 + 0.05 * (0 - 30) = 30 - 1.5 = 28.5. No division-by-zero, no NaN.
  CHECK(engine.hud.smoothed_fps == doctest::Approx(28.5f).epsilon(1e-6f));
  CHECK_FALSE(std::isnan(engine.hud.smoothed_fps));

  engine.cleanup();
}

TEST_CASE("HudOverlay: counts only the frames whose step budget was exhausted") {
  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(fs::is_directory(fixtures));

  corundum::Engine engine = make_initialised_engine(fixtures);

  engine.timer.last_frame_dt = 1.f / 60.f;
  engine.hud.enabled = true;

  const auto render_with = [&engine](const bool budget_exhausted) {
    const corundum::debug::OverlayInput input{
        .render_state = &engine.render,
        .cfg = &engine.cfg,
        .scene = &engine.scene,
        .timer = &engine.timer,
        .viewport = {.x = 320.f, .y = 240.f},
        .step_budget_exhausted = budget_exhausted,
    };
    engine.hud.render(*engine.renderer, input);
  };

  render_with(true);
  render_with(false);
  render_with(true);

  CHECK(engine.hud.shed_frames == 2u);

  engine.cleanup();
}

TEST_CASE("HudOverlay::render advances the FPS EMA but skips drawing while disabled") {
  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(fs::is_directory(fixtures));

  corundum::Engine engine = make_initialised_engine(fixtures);

  engine.timer.last_frame_dt = 1.f / 60.f;
  engine.hud.enabled = false;
  engine.hud.smoothed_fps = 0.f;

  const corundum::debug::OverlayInput input{
      .render_state = &engine.render,
      .cfg = &engine.cfg,
      .scene = &engine.scene,
      .timer = &engine.timer,
      .viewport = {.x = 320.f, .y = 240.f},
      .step_budget_exhausted = true,
  };

  engine.hud.render(*engine.renderer, input);

  // EMA still advances (0.05 * 60 = 3.0); shed frames are not counted while disabled.
  CHECK(engine.hud.smoothed_fps == doctest::Approx(3.0f).epsilon(1e-4f));
  CHECK(engine.hud.shed_frames == 0u);

  engine.cleanup();
}

// doctest's REQUIRE/CHECK macros expand to branches, so the assertion count — not the test's
// logic — dominates this metric.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("HudOverlay: world geometry uses the live viewport, not the configured window size") {
  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(fs::is_directory(fixtures));

  corundum::Engine engine = make_initialised_engine(fixtures);
  engine.hud.enabled = true;

  // cfg.win_w/win_h stay 320x240 (the startup size) while the live window is larger, as after a
  // switch to fullscreen. Projecting with cfg's size desyncs the debug geometry from the sprites,
  // which the main render draws with the live viewport, so the overlay must use input.viewport.
  const corundum::core::math::Vec2 live_viewport{.x = 640.f, .y = 480.f};
  const corundum::debug::OverlayInput input{
      .render_state = &engine.render,
      .cfg = &engine.cfg,
      .scene = &engine.scene,
      .timer = &engine.timer,
      .viewport = live_viewport,
  };

  ViewportRecordingRenderer renderer;
  engine.hud.render(renderer, input);

  REQUIRE_FALSE(renderer.viewports.empty());
  for (const corundum::core::math::Vec2 viewport : renderer.viewports) {
    CHECK(viewport.x == live_viewport.x);
    CHECK(viewport.y == live_viewport.y);
  }

  engine.cleanup();
}
