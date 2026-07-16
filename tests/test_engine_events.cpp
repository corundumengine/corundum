#include <doctest/doctest.h>

#include <corundum/engine.hpp>
#include <corundum/gameplay/flags.hpp>
#include <corundum/gameplay/quest/quest.hpp>
#include <corundum/gameplay/quest/registry.hpp>
#include <corundum/gameplay/quest/system.hpp>

namespace dialogue = corundum::gameplay::dialogue;

TEST_CASE("engine events: on_event hook handles custom event and returns true") {
  corundum::Engine engine;
  auto &pending = engine.scene.pending_dialogue_events;

  pending.push_back(dialogue::EventAction{"my_custom_event", {"arg1", "arg2"}});

  std::string captured_name;
  std::vector<std::string> captured_args;
  bool hook_called = false;

  engine.on_event = [&hook_called, &captured_name, &captured_args](corundum::Engine &,
                                                                   const dialogue::EventAction &ev) {
    hook_called = true;
    captured_name = ev.name;
    captured_args = ev.args;
    return true;
  };

  corundum::process_dialogue_events(engine);

  CHECK(hook_called);
  CHECK(captured_name == "my_custom_event");
  REQUIRE(captured_args.size() == 2);
  CHECK(captured_args[0] == "arg1");
  CHECK(captured_args[1] == "arg2");
  CHECK(pending.empty());
}

TEST_CASE("engine events: on_event hook returning false falls through to WARN") {
  corundum::Engine engine;
  auto &pending = engine.scene.pending_dialogue_events;

  pending.push_back(dialogue::EventAction{"my_custom_event", {"arg1"}});

  bool hook_called = false;
  engine.on_event = [&hook_called](corundum::Engine &, const dialogue::EventAction &) {
    hook_called = true;
    return false;
  };

  corundum::process_dialogue_events(engine);

  CHECK(hook_called);
  CHECK(pending.empty());
}

TEST_CASE("engine events: on_event hook unset — pending cleared and built-in dispatch works") {
  corundum::Engine engine;

  auto &pending = engine.scene.pending_dialogue_events;
  pending.push_back(dialogue::EventAction{"unhandled_event", {}});
  pending.push_back(dialogue::EventAction{"quest_start", {"test_quest"}});

  {
    corundum::gameplay::quest::Quest q;
    q.quest_id = "test_quest";
    q.name = "Test Quest";
    q.stages.push_back({"start", 1, false, false, {}});
    q.stages.push_back({"complete", 2, true, false, {}});
    engine.quests.add(std::move(q));
  }

  corundum::process_dialogue_events(engine);

  CHECK(pending.empty());
  CHECK(corundum::gameplay::has_flag(engine.flags, "quest.test_quest"));
}
