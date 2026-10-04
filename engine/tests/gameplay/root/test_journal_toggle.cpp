// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/engine.hpp>
#include <corundum/gameplay/gameplay.hpp>
#include <corundum/gameplay/quest/quest.hpp>
#include <corundum/gameplay/quest/system.hpp>
#include <corundum/gameplay/screens/journal.hpp>
#include <corundum/gameplay/screens/modes.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/input_intent.hpp>
#include <corundum/world/flags.hpp>
#include <corundum/world/scene.hpp>
#include <corundum/world/ui_stack.hpp>

#include "world_transition_fixtures.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

namespace fs = std::filesystem;

namespace {

  using corundum::test::adopt_platform;
  using corundum::test::make_world_config;
  using corundum::world::GameMode;
  namespace screens = corundum::gameplay::screens;
  namespace quest = corundum::gameplay::quest;

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
  void add_started_quest(corundum::Engine &engine, corundum::gameplay::Gameplay &gameplay, std::string_view id,
                         std::string name) {
    quest::Quest q;
    q.quest_id = id;
    q.name = std::move(name);
    q.stages.push_back({.name = "start", .objectives = {{.text = "Do the thing"}}, .sequence = 1});
    q.stages.push_back({.name = "done", .resolved = true, .sequence = 2});
    gameplay.quests.add(std::move(q));
    quest::start(*gameplay.quests.find(id), engine.flags);
  }

  /// Complete the quest whose id is @p id.
  void complete(corundum::Engine &engine, std::string_view id) {
    engine.flags[quest::quest_flag_key(id)] = 2;
  }

  bool tracked(const corundum::Engine &engine, std::string_view id) {
    return corundum::world::has_flag(engine.flags, quest::tracked_flag_key(id));
  }

} // namespace

TEST_CASE("journal — J toggles the panel, arrows wrap the cursor, Cancel closes") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(engine.initialize(make_world_config(fixtures)).has_value());
  corundum::gameplay::Gameplay gameplay{engine};
  REQUIRE(engine.scene.mode() == GameMode::Exploring);

  // The fixture config loads no quests; register two started quests here.
  add_started_quest(engine, gameplay, "ember", "Ember");
  add_started_quest(engine, gameplay, "salt", "Salt");

  // Press J: Exploring → Journal, cursor reset.
  press(engine, corundum::input::Action::Journal);
  CHECK(engine.scene.mode() == screens::Journal);
  CHECK(gameplay.journal_screen.cursor == 0);

  // Arrows move, wrapping over the two started quests.
  press(engine, corundum::input::Action::MoveDown);
  CHECK(gameplay.journal_screen.cursor == 1);
  press(engine, corundum::input::Action::MoveDown);
  CHECK(gameplay.journal_screen.cursor == 0);
  press(engine, corundum::input::Action::MoveUp);
  CHECK(gameplay.journal_screen.cursor == 1);

  // Press J again: Journal → Exploring.
  press(engine, corundum::input::Action::Journal);
  CHECK(engine.scene.mode() == GameMode::Exploring);

  // Esc closes it too.
  press(engine, corundum::input::Action::Journal);
  REQUIRE(engine.scene.mode() == screens::Journal);
  press(engine, corundum::input::Action::Cancel);
  CHECK(engine.scene.mode() == GameMode::Exploring);

  engine.cleanup();
}

TEST_CASE("journal — an empty journal opens without a cursor move crashing") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(engine.initialize(make_world_config(fixtures)).has_value());
  // NOLINTNEXTLINE(misc-const-correctness)
  corundum::gameplay::Gameplay gameplay{engine};

  press(engine, corundum::input::Action::Journal);
  REQUIRE(engine.scene.mode() == screens::Journal);

  press(engine, corundum::input::Action::MoveDown);
  CHECK(gameplay.journal_screen.cursor == 0);

  engine.cleanup();
}

TEST_CASE("journal — sub-tab triggers cycle tabs and reset the cursor") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(engine.initialize(make_world_config(fixtures)).has_value());
  corundum::gameplay::Gameplay gameplay{engine};
  add_started_quest(engine, gameplay, "ember", "Ember");
  add_started_quest(engine, gameplay, "salt", "Salt");

  press(engine, corundum::input::Action::Journal);
  REQUIRE(engine.scene.mode() == screens::Journal);
  CHECK(gameplay.journal_screen.tab == screens::JournalTab::Active);
  press(engine, corundum::input::Action::MoveDown);
  CHECK(gameplay.journal_screen.cursor == 1);

  // Completed tab is empty; the cursor resets to 0.
  press(engine, corundum::input::Action::SubTabNext);
  CHECK(gameplay.journal_screen.tab == screens::JournalTab::Completed);
  CHECK(gameplay.journal_screen.cursor == 0);

  press(engine, corundum::input::Action::SubTabNext);
  CHECK(gameplay.journal_screen.tab == screens::JournalTab::Failed);

  // Wraps back to Active.
  press(engine, corundum::input::Action::SubTabNext);
  CHECK(gameplay.journal_screen.tab == screens::JournalTab::Active);

  press(engine, corundum::input::Action::SubTabPrev);
  CHECK(gameplay.journal_screen.tab == screens::JournalTab::Failed);

  engine.cleanup();
}

TEST_CASE("journal — Activate tracks the highlighted Active quest, exclusively, and toggles off") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(engine.initialize(make_world_config(fixtures)).has_value());
  corundum::gameplay::Gameplay gameplay{engine};
  add_started_quest(engine, gameplay, "ember", "Ember");
  add_started_quest(engine, gameplay, "salt", "Salt");

  press(engine, corundum::input::Action::Journal);
  REQUIRE(engine.scene.mode() == screens::Journal);

  // Rows sort by name: Ember (0), Salt (1).
  press(engine, corundum::input::Action::Activate);
  CHECK(tracked(engine, "ember"));
  CHECK_FALSE(tracked(engine, "salt"));

  press(engine, corundum::input::Action::MoveDown);
  press(engine, corundum::input::Action::Activate);
  CHECK_FALSE(tracked(engine, "ember"));
  CHECK(tracked(engine, "salt"));

  // Activating the tracked quest again clears it.
  press(engine, corundum::input::Action::Activate);
  CHECK_FALSE(tracked(engine, "salt"));

  engine.cleanup();
}

TEST_CASE("journal — a tracked Active quest moves to the Completed tab when it completes") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(engine.initialize(make_world_config(fixtures)).has_value());
  corundum::gameplay::Gameplay gameplay{engine};
  add_started_quest(engine, gameplay, "ember", "Ember");

  press(engine, corundum::input::Action::Journal);
  press(engine, corundum::input::Action::Activate);
  REQUIRE(tracked(engine, "ember"));

  complete(engine, "ember");
  press(engine, corundum::input::Action::SubTabNext); // Active -> Completed
  CHECK(gameplay.journal_screen.tab == screens::JournalTab::Completed);

  engine.cleanup();
}
