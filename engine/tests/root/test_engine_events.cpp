// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/gameplay/dialogue/action.hpp>
#include <doctest/doctest.h>

#include <corundum/engine.hpp>
#include <corundum/gameplay/gameplay.hpp>
#include <corundum/gameplay/quest/quest.hpp>
#include <corundum/gameplay/quest/registry.hpp>
#include <corundum/ui/toast.hpp>
#include <corundum/world/flags.hpp>
#include <string>
#include <utility>
#include <vector>

namespace dialogue = corundum::gameplay::dialogue;

// NOLINTNEXTLINE(readability-function-cognitive-complexity): CHECK expands to branches.
TEST_CASE("engine events: on_event hook handles custom event and returns true") {
  corundum::Engine engine;
  corundum::gameplay::Gameplay gameplay{engine};
  auto &pending = engine.scene.pending_dialogue_events;

  pending.push_back(dialogue::EventAction{.name = "my_custom_event", .args = {"arg1", "arg2"}});

  std::string captured_name;
  std::vector<std::string> captured_args;
  bool hook_called = false;

  gameplay.on_event = [&hook_called, &captured_name, &captured_args](corundum::Engine &,
                                                                     const dialogue::EventAction &ev) {
    hook_called = true;
    captured_name = ev.name;
    captured_args = ev.args;
    return true;
  };

  gameplay.process_events();

  CHECK(hook_called);
  CHECK(captured_name == "my_custom_event");
  REQUIRE(captured_args.size() == 2);
  CHECK(captured_args[0] == "arg1");
  CHECK(captured_args[1] == "arg2");
  CHECK(pending.empty());
}

TEST_CASE("engine events: on_event hook returning false falls through to WARN") {
  corundum::Engine engine;
  corundum::gameplay::Gameplay gameplay{engine};
  auto &pending = engine.scene.pending_dialogue_events;

  pending.push_back(dialogue::EventAction{.name = "my_custom_event", .args = {"arg1"}});

  bool hook_called = false;
  gameplay.on_event = [&hook_called](corundum::Engine &, const dialogue::EventAction &) {
    hook_called = true;
    return false;
  };

  gameplay.process_events();

  CHECK(hook_called);
  CHECK(pending.empty());
}

TEST_CASE("engine events: on_event hook unset — pending cleared and built-in dispatch works") {
  corundum::Engine engine;
  corundum::gameplay::Gameplay gameplay{engine};

  auto &pending = engine.scene.pending_dialogue_events;
  pending.push_back(dialogue::EventAction{.name = "unhandled_event", .args = {}});
  pending.push_back(dialogue::EventAction{.name = "quest_start", .args = {"test_quest"}});

  {
    corundum::gameplay::quest::Quest q;
    q.quest_id = "test_quest";
    q.name = "Test Quest";
    q.stages.push_back({.name = "start", .sequence = 1});
    q.stages.push_back({.name = "complete", .resolved = true, .sequence = 2});
    gameplay.quests.add(std::move(q));
  }

  gameplay.process_events();

  CHECK(pending.empty());
  CHECK(corundum::world::has_flag(engine.flags, "quest.test_quest"));
}

TEST_CASE("engine events: quest_start enqueues a start toast with the quest name") {
  corundum::Engine engine;
  corundum::gameplay::Gameplay gameplay{engine};
  corundum::gameplay::quest::Quest q;
  q.quest_id = "ember";
  q.name = "Ember of Greyhollow";
  q.stages.push_back({.name = "start", .sequence = 1});
  q.stages.push_back({.name = "done", .resolved = true, .sequence = 2});
  gameplay.quests.add(std::move(q));

  engine.scene.pending_dialogue_events.push_back(dialogue::EventAction{.name = "quest_start", .args = {"ember"}});
  gameplay.process_events();

  REQUIRE(engine.toasts.size() == 1);
  CHECK(engine.toasts.at(0).text == "Quest started: Ember of Greyhollow");
}

TEST_CASE("engine events: re-starting an underway quest does not toast again") {
  corundum::Engine engine;
  corundum::gameplay::Gameplay gameplay{engine};
  corundum::gameplay::quest::Quest q;
  q.quest_id = "ember";
  q.name = "Ember";
  q.stages.push_back({.name = "start", .sequence = 1});
  q.stages.push_back({.name = "done", .resolved = true, .sequence = 2});
  gameplay.quests.add(std::move(q));

  engine.scene.pending_dialogue_events.push_back(dialogue::EventAction{.name = "quest_start", .args = {"ember"}});
  gameplay.process_events();
  REQUIRE(engine.toasts.size() == 1);

  engine.scene.pending_dialogue_events.push_back(dialogue::EventAction{.name = "quest_start", .args = {"ember"}});
  gameplay.process_events();
  CHECK(engine.toasts.size() == 1);
}

TEST_CASE("engine events: quest_advance to a live stage toasts an update") {
  corundum::Engine engine;
  corundum::gameplay::Gameplay gameplay{engine};
  corundum::gameplay::quest::Quest q;
  q.quest_id = "ember";
  q.name = "Ember";
  q.stages.push_back({.name = "start", .sequence = 1});
  q.stages.push_back({.name = "investigate", .sequence = 2});
  q.stages.push_back({.name = "done", .resolved = true, .sequence = 3});
  gameplay.quests.add(std::move(q));
  engine.flags["quest.ember"] = 1;

  engine.scene.pending_dialogue_events.push_back(
      dialogue::EventAction{.name = "quest_advance", .args = {"ember", "investigate"}});
  gameplay.process_events();

  REQUIRE(engine.toasts.size() == 1);
  CHECK(engine.toasts.at(0).text == "Quest updated: Ember");
  CHECK(engine.toasts.at(0).colour.r == corundum::ui::k_toast_updated_colour.r);
}

TEST_CASE("engine events: quest_advance to a resolved stage toasts completion") {
  corundum::Engine engine;
  corundum::gameplay::Gameplay gameplay{engine};
  corundum::gameplay::quest::Quest q;
  q.quest_id = "ember";
  q.name = "Ember";
  q.stages.push_back({.name = "start", .sequence = 1});
  q.stages.push_back({.name = "done", .resolved = true, .sequence = 2});
  gameplay.quests.add(std::move(q));
  engine.flags["quest.ember"] = 1;

  engine.scene.pending_dialogue_events.push_back(
      dialogue::EventAction{.name = "quest_advance", .args = {"ember", "done"}});
  gameplay.process_events();

  REQUIRE(engine.toasts.size() == 1);
  CHECK(engine.toasts.at(0).text == "Quest complete: Ember");
}

TEST_CASE("engine events: quest_advance to a failed stage toasts failure") {
  corundum::Engine engine;
  corundum::gameplay::Gameplay gameplay{engine};
  corundum::gameplay::quest::Quest q;
  q.quest_id = "ember";
  q.name = "Ember";
  q.stages.push_back({.name = "start", .sequence = 1});
  q.stages.push_back({.failed = true, .name = "lost", .resolved = true, .sequence = 2});
  gameplay.quests.add(std::move(q));
  engine.flags["quest.ember"] = 1;

  engine.scene.pending_dialogue_events.push_back(
      dialogue::EventAction{.name = "quest_advance", .args = {"ember", "lost"}});
  gameplay.process_events();

  REQUIRE(engine.toasts.size() == 1);
  CHECK(engine.toasts.at(0).text == "Quest failed: Ember");
}

TEST_CASE("engine events: an unknown advance stage does not toast") {
  corundum::Engine engine;
  corundum::gameplay::Gameplay gameplay{engine};
  corundum::gameplay::quest::Quest q;
  q.quest_id = "ember";
  q.name = "Ember";
  q.stages.push_back({.name = "start", .sequence = 1});
  q.stages.push_back({.name = "done", .resolved = true, .sequence = 2});
  gameplay.quests.add(std::move(q));
  engine.flags["quest.ember"] = 1;

  engine.scene.pending_dialogue_events.push_back(
      dialogue::EventAction{.name = "quest_advance", .args = {"ember", "nope"}});
  gameplay.process_events();

  CHECK(engine.toasts.empty());
}

TEST_CASE("engine events: give_item adds item.<id> count to flags") {
  corundum::Engine engine;
  corundum::gameplay::Gameplay gameplay{engine};
  engine.scene.pending_dialogue_events.push_back(dialogue::EventAction{.name = "give_item", .args = {"gold", "5"}});
  gameplay.process_events();

  CHECK(engine.flags["item.gold"] == 5);
}

TEST_CASE("engine events: give_item without a count defaults to +1") {
  corundum::Engine engine;
  corundum::gameplay::Gameplay gameplay{engine};
  engine.scene.pending_dialogue_events.push_back(dialogue::EventAction{.name = "give_item", .args = {"salt"}});
  gameplay.process_events();

  CHECK(engine.flags["item.salt"] == 1);
}

TEST_CASE("engine events: take_item subtracts and erases the key at or below zero") {
  corundum::Engine engine;
  corundum::gameplay::Gameplay gameplay{engine};
  engine.flags["item.gold"] = 5;

  engine.scene.pending_dialogue_events.push_back(dialogue::EventAction{.name = "take_item", .args = {"gold", "2"}});
  gameplay.process_events();
  CHECK(engine.flags["item.gold"] == 3);

  engine.scene.pending_dialogue_events.push_back(dialogue::EventAction{.name = "take_item", .args = {"gold", "10"}});
  gameplay.process_events();
  CHECK(!engine.flags.contains("item.gold"));
}

TEST_CASE("engine events: take_item on a missing item is a no-op") {
  corundum::Engine engine;
  corundum::gameplay::Gameplay gameplay{engine};
  engine.scene.pending_dialogue_events.push_back(dialogue::EventAction{.name = "take_item", .args = {"gold", "1"}});
  gameplay.process_events();

  CHECK(!engine.flags.contains("item.gold"));
}

TEST_CASE("engine events: reputation accumulates rep.<faction> and can go negative") {
  corundum::Engine engine;
  corundum::gameplay::Gameplay gameplay{engine};
  engine.scene.pending_dialogue_events.push_back(dialogue::EventAction{.name = "reputation", .args = {"village", "3"}});
  engine.scene.pending_dialogue_events.push_back(
      dialogue::EventAction{.name = "reputation", .args = {"village", "-1"}});
  gameplay.process_events();

  CHECK(engine.flags["rep.village"] == 2);
}
