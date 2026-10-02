// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/engine.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/quest/quest.hpp>
#include <corundum/quest/system.hpp>
#include <corundum/world/scene.hpp>

#include "world_transition_fixtures.hpp"

#include <filesystem>
#include <string>
#include <utility>

namespace fs = std::filesystem;

namespace {

  using corundum::test::adopt_platform;
  using corundum::test::advance_with;
  using corundum::test::make_world_config;

  /// Register a two-stage quest and start it, so the journal has one Active row.
  void add_started_quest(corundum::Engine &engine, std::string id, std::string name) {
    const std::string quest_id = id;
    corundum::quest::Quest q;
    q.quest_id = std::move(id);
    q.name = std::move(name);
    q.stages.push_back({.name = "start", .objectives = {{.text = "Do the thing"}}, .sequence = 1});
    q.stages.push_back({.name = "done", .resolved = true, .sequence = 2});
    engine.quests.add(std::move(q));
    corundum::quest::start(*engine.quests.find(quest_id), engine.flags);
  }

} // namespace

TEST_CASE("journal — J toggles the panel, arrows wrap the cursor, Cancel closes") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(engine.initialize(make_world_config(fixtures)).has_value());
  using corundum::world::GameMode;
  REQUIRE(engine.scene.mode == GameMode::Exploring);

  // The fixture config loads no quests; register two started quests here.
  add_started_quest(engine, "ember", "Ember");
  add_started_quest(engine, "salt", "Salt");

  // Press J: Exploring → Journal, cursor reset.
  advance_with(engine, corundum::input::Action::Journal);
  CHECK(engine.scene.mode == GameMode::Journal);
  CHECK(engine.scene.journal_cursor == 0);

  // Arrows move, wrapping over the two started quests.
  advance_with(engine, corundum::input::Action::MoveDown);
  CHECK(engine.scene.journal_cursor == 1);
  advance_with(engine, corundum::input::Action::MoveDown);
  CHECK(engine.scene.journal_cursor == 0);
  advance_with(engine, corundum::input::Action::MoveUp);
  CHECK(engine.scene.journal_cursor == 1);

  // Press J again: Journal → Exploring.
  advance_with(engine, corundum::input::Action::Journal);
  CHECK(engine.scene.mode == GameMode::Exploring);

  // Esc closes it too.
  advance_with(engine, corundum::input::Action::Journal);
  REQUIRE(engine.scene.mode == GameMode::Journal);
  advance_with(engine, corundum::input::Action::Cancel);
  CHECK(engine.scene.mode == GameMode::Exploring);

  engine.cleanup();
}

TEST_CASE("journal — an empty journal opens without a cursor move crashing") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(engine.initialize(make_world_config(fixtures)).has_value());
  using corundum::world::GameMode;

  advance_with(engine, corundum::input::Action::Journal);
  REQUIRE(engine.scene.mode == GameMode::Journal);

  advance_with(engine, corundum::input::Action::MoveDown);
  CHECK(engine.scene.journal_cursor == 0);

  engine.cleanup();
}
