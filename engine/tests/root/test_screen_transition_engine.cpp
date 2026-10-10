// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/engine.hpp>
#include <corundum/render/render_state.hpp>
#include <corundum/ui/screen_transition.hpp>
#include <corundum/world/portals/portal.hpp>
#include <corundum/world/transition.hpp>
#include <corundum/world/ui_stack.hpp>

#include "world_transition_fixtures.hpp"

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

  using corundum::test::adopt_platform;
  using corundum::test::make_world_config;
  using corundum::ui::TransitionPhase;
  using corundum::world::GameMode;
  using corundum::world::MapTransition;

  /// Advance exactly one fixed step; run_frame() renders through the null renderer.
  void step(corundum::Engine &engine) {
    engine.timer.accumulator = engine.timer.target_dt;
    static_cast<void>(engine.run_frame());
  }

  std::string interior_path(const fs::path &fixtures) {
    return (fixtures / "tilemaps/interior.json").string();
  }

} // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity): CHECK expands to branches.
TEST_CASE("engine screen transition — a UI screen push or pop never starts a fade") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);
  REQUIRE(engine.initialize(make_world_config(CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR)).has_value());
  REQUIRE_FALSE(engine.screen_transition.active());

  engine.scene.ui.push(GameMode::Menu);
  for (int i = 0; i < 4; ++i)
    step(engine);
  CHECK_FALSE(engine.screen_transition.active());
  CHECK_EQ(engine.screen_transition.alpha(), 0.f);

  engine.scene.ui.pop();
  for (int i = 0; i < 4; ++i)
    step(engine);
  CHECK_FALSE(engine.screen_transition.active());
  CHECK_EQ(engine.screen_transition.alpha(), 0.f);

  engine.cleanup();
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): CHECK expands to branches.
TEST_CASE("engine screen transition — a portal transition fades out, swaps at black, then fades in") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);
  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(engine.initialize(make_world_config(fixtures)).has_value());

  engine.scene.pending_transition =
      MapTransition{.target_map = interior_path(fixtures), .spawn_col = 1, .spawn_row = 2, .return_to_world = false};

  // First frame arms the fade-out but leaves the scene untouched.
  engine.advance_scene_transition();
  CHECK(engine.screen_transition.phase() == TransitionPhase::FadingOut);
  CHECK(engine.render.mode == corundum::render::RenderMode::World);
  REQUIRE(engine.scene.pending_transition.has_value());

  // Once black, the swap runs and the new scene starts fading in.
  engine.screen_transition.update(corundum::ui::ScreenTransition::k_fade_duration_seconds);
  REQUIRE(engine.screen_transition.at_black());
  engine.advance_scene_transition();
  CHECK(engine.render.mode == corundum::render::RenderMode::SingleMap);
  CHECK_FALSE(engine.scene.pending_transition.has_value());
  CHECK(engine.screen_transition.phase() == TransitionPhase::FadingIn);

  engine.cleanup();
}

TEST_CASE("engine screen transition — no pending transition leaves the fade untouched") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);
  REQUIRE(engine.initialize(make_world_config(CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR)).has_value());

  engine.advance_scene_transition();
  CHECK_FALSE(engine.screen_transition.active());
  CHECK_EQ(engine.screen_transition.alpha(), 0.f);

  engine.cleanup();
}
