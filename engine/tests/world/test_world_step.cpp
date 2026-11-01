// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/engine.hpp>
#include <corundum/entities/world.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/screen_registry.hpp>
#include <corundum/world/map_view.hpp>
#include <corundum/world/picking.hpp>
#include <corundum/world/ui_stack.hpp>
#include <corundum/world/update.hpp>

#include "world_transition_fixtures.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <tuple>
#include <vector>

namespace {

  using corundum::test::adopt_platform;
  using corundum::test::initialize_with_world;
  using corundum::test::make_world_config;
  using corundum::world::WorldStep;

  /// An extension mode no framework registers; a test registers the screen it needs.
  constexpr corundum::world::GameMode k_test_mode{
      static_cast<corundum::world::GameMode>(corundum::world::k_first_extension_mode + 40),
  };

  void place_player(corundum::Engine &engine, float col, float row) {
    auto &transforms = engine.scene.world.transforms;
    const auto slot = transforms.dense_index(engine.scene.player);
    transforms.col[slot] = col;
    transforms.row[slot] = row;
  }

  /// The fixture world with the player on (8, 8), a path queued to (9, 8), and k_test_mode on top.
  std::unique_ptr<corundum::Engine> make_walking_engine() {
    auto engine = std::make_unique<corundum::Engine>();
    adopt_platform(*engine, 320, 240);
    REQUIRE(initialize_with_world(*engine, make_world_config(CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR)).has_value());
    place_player(*engine, 8.f, 8.f);
    engine->scene.path = {corundum::world::TileCoord{.col = 9, .row = 8}};
    engine->scene.ui.push(k_test_mode);
    return engine;
  }

  void update_once(corundum::Engine &engine, const corundum::input::InputState &input, WorldStep world_step) {
    const corundum::world::MapView map = corundum::world::build_map_view(engine.render, engine.cfg);
    corundum::world::update(engine.scene, engine.cfg, input, map, 1.f / 60.f, static_cast<float>(engine.window_width()),
                            static_cast<float>(engine.window_height()), engine.input_mapper.last_device(), world_step);
  }

  float player_col(const corundum::Engine &engine) {
    return engine.scene.world.transforms.pos_col(engine.scene.player);
  }

} // namespace

TEST_CASE("world step: a mode that runs the world walks the player along its path") {
  const std::unique_ptr<corundum::Engine> engine = make_walking_engine();
  const corundum::input::InputState no_input{};

  update_once(*engine, no_input, WorldStep::Runs);

  CHECK_GT(player_col(*engine), 8.f);
  engine->cleanup();
}

TEST_CASE("world step: a frozen mode leaves the player and its path alone") {
  const std::unique_ptr<corundum::Engine> engine = make_walking_engine();
  const corundum::input::InputState no_input{};

  update_once(*engine, no_input, WorldStep::Frozen);

  CHECK_EQ(player_col(*engine), doctest::Approx(8.f));
  CHECK_EQ(engine->scene.path, std::vector<corundum::world::TileCoord>{{.col = 9, .row = 8}});
  engine->cleanup();
}

TEST_CASE("world step: a mode that runs the world targets no interaction") {
  const std::unique_ptr<corundum::Engine> engine = make_walking_engine();
  // a00 stands on (2, 3) with a dialogue; one tile south of it is in interact range.
  place_player(*engine, 2.f, 4.f);
  engine->scene.path.clear();
  corundum::input::InputState interact{};
  interact.pressed.set(static_cast<std::size_t>(corundum::input::Action::Activate));
  engine->scene.ui.pop();
  update_once(*engine, interact, WorldStep::Frozen);
  REQUIRE(engine->scene.pending_interaction.has_value());
  engine->scene.ui.push(k_test_mode);

  update_once(*engine, interact, WorldStep::Runs);

  CHECK_FALSE(engine->scene.pending_interaction.has_value());
  engine->cleanup();
}

TEST_CASE("world step: the engine runs the world beneath a screen registered with WorldStep::Runs") {
  const std::unique_ptr<corundum::Engine> engine = make_walking_engine();
  engine->screens.add(k_test_mode, corundum::ScreenSpec{.owns_step = false, .world_step = WorldStep::Runs});
  engine->timer.accumulator = engine->timer.target_dt;

  std::ignore = engine->run_frame();

  CHECK_GT(player_col(*engine), 8.f);
  engine->cleanup();
}
