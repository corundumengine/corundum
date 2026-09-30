// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/engine.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/world/scene.hpp>

#include "world_transition_fixtures.hpp"

#include <filesystem>

namespace fs = std::filesystem;

namespace {

  using corundum::test::adopt_platform;
  using corundum::test::advance_with;
  using corundum::test::make_world_config;

} // namespace

TEST_CASE("inventory — I toggles the panel and freezes the player, arrows move the cursor") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(engine.initialize(make_world_config(fixtures)).has_value());
  using corundum::world::GameMode;
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

  engine.cleanup();
}
