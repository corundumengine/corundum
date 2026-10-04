// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/engine.hpp>
#include <corundum/gameplay/gameplay.hpp>
#include <corundum/gameplay/screens/modes.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/input_intent.hpp>
#include <corundum/world/scene.hpp>
#include <corundum/world/ui_stack.hpp>

#include "world_transition_fixtures.hpp"

#include <cstddef>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

  using corundum::test::adopt_platform;
  using corundum::test::make_world_config;
  using corundum::world::GameMode;
  namespace screens = corundum::gameplay::screens;

  /// Route one physical action press through the engine's UI step, exactly as the fixed-step
  /// loop does. Inventory is an engine-owned hub tab, stepped by Engine, not world::update, so
  /// the shared advance_with() helper cannot drive it.
  bool press(corundum::Engine &engine, corundum::input::Action action) {
    corundum::input::InputState state{};
    state.pressed.set(static_cast<std::size_t>(action));
    engine.input_state = state;
    const auto intent = corundum::input::make_input_intent(engine.input_state, engine.input_mapper.last_device());
    return engine.update_engine_screens(intent);
  }

} // namespace

TEST_CASE("inventory — I toggles the panel and freezes the player, arrows move the cursor") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(engine.initialize(make_world_config(fixtures)).has_value());
  // NOLINTNEXTLINE(misc-const-correctness)
  corundum::gameplay::Gameplay gameplay{engine};
  REQUIRE(engine.scene.mode() == GameMode::Exploring);
  REQUIRE(gameplay.inventory_cursor == 0);

  // Three held items → three rows to wrap within.
  engine.flags["item.a"] = 1;
  engine.flags["item.b"] = 2;
  engine.flags["item.c"] = 1;

  // Press I: Exploring → Inventory, cursor reset.
  press(engine, corundum::input::Action::Inventory);
  CHECK(engine.scene.mode() == screens::Inventory);
  CHECK(gameplay.inventory_cursor == 0);

  // Arrows move the highlight while paused.
  press(engine, corundum::input::Action::MoveDown);
  CHECK(engine.scene.mode() == screens::Inventory);
  CHECK(gameplay.inventory_cursor == 1);
  press(engine, corundum::input::Action::MoveUp);
  CHECK(gameplay.inventory_cursor == 0);

  // Down past the last row wraps to the first (dialogue choice-list behaviour).
  press(engine, corundum::input::Action::MoveDown);
  press(engine, corundum::input::Action::MoveDown);
  press(engine, corundum::input::Action::MoveDown);
  CHECK(gameplay.inventory_cursor == 0);

  // Up past the first row wraps to the last.
  press(engine, corundum::input::Action::MoveUp);
  CHECK(gameplay.inventory_cursor == 2);

  // Press I again: Inventory → Exploring.
  press(engine, corundum::input::Action::Inventory);
  CHECK(engine.scene.mode() == GameMode::Exploring);

  // Esc also closes an open panel.
  press(engine, corundum::input::Action::Inventory);
  REQUIRE(engine.scene.mode() == screens::Inventory);
  press(engine, corundum::input::Action::Cancel);
  CHECK(engine.scene.mode() == GameMode::Exploring);

  // Opening again resets the cursor to the top.
  press(engine, corundum::input::Action::MoveDown);
  press(engine, corundum::input::Action::Inventory);
  CHECK(engine.scene.mode() == screens::Inventory);
  CHECK(gameplay.inventory_cursor == 0);

  engine.cleanup();
}

TEST_CASE("inventory — rows are built once on open and not rebuilt while it stays open") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(engine.initialize(make_world_config(fixtures)).has_value());
  // NOLINTNEXTLINE(misc-const-correctness)
  corundum::gameplay::Gameplay gameplay{engine};

  engine.flags["item.a"] = 1;
  engine.flags["item.b"] = 1;

  press(engine, corundum::input::Action::Inventory);
  REQUIRE(engine.scene.mode() == screens::Inventory);
  REQUIRE(gameplay.inventory_lines.size() == 2);

  // The inventory is read-only while open: a flag mutated from outside must not change the
  // cached rows until the tab is reopened (the whole point of building once on open).
  engine.flags["item.c"] = 1;
  press(engine, corundum::input::Action::MoveDown);
  CHECK(gameplay.inventory_lines.size() == 2);

  // Reopening rebuilds from the current flags.
  press(engine, corundum::input::Action::Cancel);
  press(engine, corundum::input::Action::Inventory);
  CHECK(gameplay.inventory_lines.size() == 3);

  engine.cleanup();
}
