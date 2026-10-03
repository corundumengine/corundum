// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/engine.hpp>
#include <corundum/gameplay/quest/quest.hpp>
#include <corundum/gameplay/quest/system.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/input_intent.hpp>
#include <corundum/world/scene.hpp>
#include <corundum/world/ui_stack.hpp>

#include "world_transition_fixtures.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <utility>

namespace fs = std::filesystem;

namespace {

  using corundum::test::adopt_platform;
  using corundum::test::make_world_config;
  using corundum::world::GameMode;

  /// Route one physical action press through the engine's UI step. Journal is an engine-owned hub
  /// tab, stepped by Engine, not world::update.
  bool press(corundum::Engine &engine, corundum::input::Action action) {
    corundum::input::InputState state{};
    state.pressed.set(static_cast<std::size_t>(action));
    engine.input_state = state;
    const auto intent = corundum::input::make_input_intent(engine.input_state, engine.input_mapper.last_device());
    return engine.update_engine_screens(intent);
  }

  /// Register a two-stage quest and start it, so the journal has one Active row.
  void add_started_quest(corundum::Engine &engine, std::string id, std::string name) {
    const std::string quest_id = id;
    corundum::gameplay::quest::Quest q;
    q.quest_id = std::move(id);
    q.name = std::move(name);
    q.stages.push_back({.name = "start", .objectives = {{.text = "Do the thing"}}, .sequence = 1});
    q.stages.push_back({.name = "done", .resolved = true, .sequence = 2});
    engine.quests.add(std::move(q));
    corundum::gameplay::quest::start(*engine.quests.find(quest_id), engine.flags);
  }

} // namespace

TEST_CASE("journal — J toggles the panel, arrows wrap the cursor, Cancel closes") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(engine.initialize(make_world_config(fixtures)).has_value());
  REQUIRE(engine.scene.mode() == GameMode::Exploring);

  // The fixture config loads no quests; register two started quests here.
  add_started_quest(engine, "ember", "Ember");
  add_started_quest(engine, "salt", "Salt");

  // Press J: Exploring → Journal, cursor reset.
  press(engine, corundum::input::Action::Journal);
  CHECK(engine.scene.mode() == GameMode::Journal);
  CHECK(engine.scene.journal_cursor == 0);

  // Arrows move, wrapping over the two started quests.
  press(engine, corundum::input::Action::MoveDown);
  CHECK(engine.scene.journal_cursor == 1);
  press(engine, corundum::input::Action::MoveDown);
  CHECK(engine.scene.journal_cursor == 0);
  press(engine, corundum::input::Action::MoveUp);
  CHECK(engine.scene.journal_cursor == 1);

  // Press J again: Journal → Exploring.
  press(engine, corundum::input::Action::Journal);
  CHECK(engine.scene.mode() == GameMode::Exploring);

  // Esc closes it too.
  press(engine, corundum::input::Action::Journal);
  REQUIRE(engine.scene.mode() == GameMode::Journal);
  press(engine, corundum::input::Action::Cancel);
  CHECK(engine.scene.mode() == GameMode::Exploring);

  engine.cleanup();
}

TEST_CASE("journal — an empty journal opens without a cursor move crashing") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(engine.initialize(make_world_config(fixtures)).has_value());

  press(engine, corundum::input::Action::Journal);
  REQUIRE(engine.scene.mode() == GameMode::Journal);

  press(engine, corundum::input::Action::MoveDown);
  CHECK(engine.scene.journal_cursor == 0);

  engine.cleanup();
}
