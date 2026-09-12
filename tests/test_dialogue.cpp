// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

#include <corundum/core/json_io.hpp>
#include <corundum/dialogue/action.hpp>
#include <corundum/dialogue/conversation.hpp>
#include <corundum/dialogue/dialogue.hpp>
#include <corundum/dialogue/expr.hpp>
#include <corundum/dialogue/loader.hpp>
#include <corundum/dialogue/query.hpp>
#include <corundum/dialogue/registry.hpp>
#include <corundum/dialogue/serialize.hpp>
#include <corundum/dialogue/validate_refs.hpp>
#include <corundum/item/registry.hpp>
#include <corundum/quest/registry.hpp>
#include <corundum/quest/system.hpp>
#include <corundum/world/flags.hpp>

// ── Helpers ───────────────────────────────────────────────────────────────────

// Builds an in-memory graph matching the innkeeper scenario (new schema):
//   n0 (Talk) → n1 (Choice)
//     choice 0: "I need a room." — condition: gold >= 5 && !paid_innkeeper
//                                  actions: gold -= 5, paid_innkeeper = true
//                                  sequence: Once → n_pay
//     choice 1: "I need a room." — condition: paid_innkeeper == true → n_already_paid
//     choice 2: "Just passing through." → n_bye
//   n_pay   (Event) actions: play_sound('coin') → n2
//   n2      (Talk)  → end
//   n_already_paid (Talk) → end
//   n_bye   (Talk)  → end
static corundum::dialogue::Graph make_innkeeper_graph() {
  using namespace corundum::dialogue;

  Graph g;
  g.graph_id = "innkeeper_intro";
  g.speaker = "Innkeeper";
  g.variables = {{"gold", 10}};

  auto push = [&g](Node n) {
    g.id_to_index[n.id] = g.nodes.size();
    g.nodes.push_back(std::move(n));
  };

  { // n0 — talk
    Node n;
    n.id = "n0";
    n.type = NodeType::Talk;
    n.text = "Welcome, traveller. What brings you here?";
    n.next_id = "n1";
    push(std::move(n));
  }
  { // n1 — choice
    Node n;
    n.id = "n1";
    n.type = NodeType::Choice;
    n.choices = {
        {.label = "I need a room.",
         .target_id = "n_pay",
         .condition = *compile("gold >= 5 && !paid_innkeeper"),
         .actions = {"gold -= 5", "paid_innkeeper = true"},
         .sequence = SequenceMode::Once},
        {.label = "I need a room.", .target_id = "n_already_paid", .condition = *compile("paid_innkeeper == true")},
        {.label = "Just passing through.", .target_id = "n_bye"},
    };
    push(std::move(n));
  }
  { // n_pay — event
    Node n;
    n.id = "n_pay";
    n.type = NodeType::Event;
    n.next_id = "n2";
    n.actions = {"play_sound('coin')"};
    push(std::move(n));
  }
  { // n2 — talk
    Node n;
    n.id = "n2";
    n.type = NodeType::Talk;
    n.text = "That'll be 5 gold pieces. Right this way.";
    n.next_id = "end";
    push(std::move(n));
  }
  { // n_already_paid — talk
    Node n;
    n.id = "n_already_paid";
    n.type = NodeType::Talk;
    n.text = "You've already paid. Your room is the last one on the left. Sleep well.";
    n.next_id = "end";
    push(std::move(n));
  }
  { // n_bye — talk
    Node n;
    n.id = "n_bye";
    n.type = NodeType::Talk;
    n.text = "Safe travels then. Watch the road south.";
    n.next_id = "end";
    push(std::move(n));
  }

  return g;
}

// ── Shared graph/test helpers ────────────────────────────────────────────────

namespace {

  /// Pushes a node into a graph, maintaining the id→index map.
  void push_node(corundum::dialogue::Graph &g, corundum::dialogue::Node n) {
    g.id_to_index[n.id] = g.nodes.size();
    g.nodes.push_back(std::move(n));
  }

  corundum::input::PressedActions select_press() {
    corundum::input::PressedActions select{};
    select.actions[0] = corundum::input::Action::Select;
    select.count = 1;
    return select;
  }

} // namespace

// ── eval_condition ────────────────────────────────────────────────────────────

TEST_CASE("eval_condition: empty expr returns true") {
  corundum::world::FlagStore flags;
  auto res = corundum::dialogue::eval_condition("", flags);
  REQUIRE(res.has_value());
  CHECK(*res == true);
}

TEST_CASE("eval_condition: integer literal truthy/falsy") {
  corundum::world::FlagStore flags;
  CHECK(*corundum::dialogue::eval_condition("1", flags) == true);
  CHECK(*corundum::dialogue::eval_condition("0", flags) == false);
  CHECK(*corundum::dialogue::eval_condition("42", flags) == true);
}

TEST_CASE("eval_condition: boolean literals") {
  corundum::world::FlagStore flags;
  CHECK(*corundum::dialogue::eval_condition("true", flags) == true);
  CHECK(*corundum::dialogue::eval_condition("false", flags) == false);
}

TEST_CASE("eval_condition: identifier resolves from FlagStore") {
  corundum::world::FlagStore flags;
  corundum::world::set_flag(flags, "paid");

  CHECK(*corundum::dialogue::eval_condition("paid", flags) == true);
  CHECK(*corundum::dialogue::eval_condition("other", flags) == false);
}

TEST_CASE("eval_condition: comparison operators") {
  corundum::world::FlagStore flags;
  flags["gold"] = 10;

  CHECK(*corundum::dialogue::eval_condition("gold == 10", flags) == true);
  CHECK(*corundum::dialogue::eval_condition("gold != 10", flags) == false);
  CHECK(*corundum::dialogue::eval_condition("gold >= 5", flags) == true);
  CHECK(*corundum::dialogue::eval_condition("gold > 10", flags) == false);
  CHECK(*corundum::dialogue::eval_condition("gold < 11", flags) == true);
  CHECK(*corundum::dialogue::eval_condition("gold <= 10", flags) == true);
}

TEST_CASE("eval_condition: boolean operators") {
  corundum::world::FlagStore flags;
  flags["gold"] = 10;

  CHECK(*corundum::dialogue::eval_condition("gold >= 5 && gold < 20", flags) == true);
  CHECK(*corundum::dialogue::eval_condition("gold > 20 || gold == 10", flags) == true);
  CHECK(*corundum::dialogue::eval_condition("!(gold == 10)", flags) == false);
}

TEST_CASE("eval_condition: bool comparison uses truthiness") {
  corundum::world::FlagStore flags;
  corundum::world::set_flag(flags, "paid"); // count = 1
  corundum::world::set_flag(flags, "paid"); // count = 2

  // paid == true should be truthy (count != 0), not strictly equal to 1
  CHECK(*corundum::dialogue::eval_condition("paid == true", flags) == true);
  CHECK(*corundum::dialogue::eval_condition("paid == false", flags) == false);
}

TEST_CASE("eval_condition: combined innkeeper condition") {
  corundum::world::FlagStore flags;
  flags["gold"] = 10;

  CHECK(*corundum::dialogue::eval_condition("gold >= 5 && !paid_innkeeper", flags) == true);

  flags["paid_innkeeper"] = 1;
  CHECK(*corundum::dialogue::eval_condition("gold >= 5 && !paid_innkeeper", flags) == false);
}

TEST_CASE("eval_condition: parse error returns ExprError") {
  corundum::world::FlagStore flags;
  auto res = corundum::dialogue::eval_condition("gold >=", flags);
  CHECK_FALSE(res.has_value());
  CHECK_FALSE(res.error().message.empty());
}

// ── parse_action ──────────────────────────────────────────────────────────────

TEST_CASE("parse_action: assign integer") {
  auto r = corundum::dialogue::parse_action("gold = 5");
  REQUIRE(r.has_value());
  const auto &sa = std::get<corundum::dialogue::StateAction>(*r);
  CHECK(sa.var == "gold");
  CHECK(sa.op == corundum::dialogue::StateAction::Op::Assign);
  CHECK(sa.value == 5);
}

TEST_CASE("parse_action: assign true/false") {
  auto r_true = corundum::dialogue::parse_action("paid = true");
  REQUIRE(r_true.has_value());
  CHECK(std::get<corundum::dialogue::StateAction>(*r_true).value == 1);

  auto r_false = corundum::dialogue::parse_action("paid = false");
  REQUIRE(r_false.has_value());
  CHECK(std::get<corundum::dialogue::StateAction>(*r_false).value == 0);
}

TEST_CASE("parse_action: add and subtract") {
  auto r_add = corundum::dialogue::parse_action("gold += 3");
  REQUIRE(r_add.has_value());
  const auto &sa = std::get<corundum::dialogue::StateAction>(*r_add);
  CHECK(sa.op == corundum::dialogue::StateAction::Op::Add);
  CHECK(sa.value == 3);

  auto r_sub = corundum::dialogue::parse_action("gold -= 5");
  REQUIRE(r_sub.has_value());
  CHECK(std::get<corundum::dialogue::StateAction>(*r_sub).op == corundum::dialogue::StateAction::Op::Sub);
}

TEST_CASE("parse_action: event call") {
  auto r = corundum::dialogue::parse_action("play_sound('coin')");
  REQUIRE(r.has_value());
  const auto &ev = std::get<corundum::dialogue::EventAction>(*r);
  CHECK(ev.name == "play_sound");
  REQUIRE(ev.args.size() == 1);
  CHECK(ev.args[0] == "coin");
}

TEST_CASE("parse_action: event call with multiple args") {
  auto r = corundum::dialogue::parse_action("trigger_event('play_sound', 'inn_door')");
  REQUIRE(r.has_value());
  const auto &ev = std::get<corundum::dialogue::EventAction>(*r);
  CHECK(ev.name == "trigger_event");
  REQUIRE(ev.args.size() == 2);
  CHECK(ev.args[0] == "play_sound");
  CHECK(ev.args[1] == "inn_door");
}

TEST_CASE("parse_action: parse error returns ActionError") {
  auto r = corundum::dialogue::parse_action("???");
  CHECK_FALSE(r.has_value());
  CHECK_FALSE(r.error().message.empty());
}

// ── execute_actions ───────────────────────────────────────────────────────────

TEST_CASE("execute_actions applies state mutations and returns event actions") {
  corundum::world::FlagStore flags;
  flags["gold"] = 10;

  const std::vector<std::string> actions = {
      "gold -= 5",
      "paid_innkeeper = true",
      "play_sound('coin')",
  };

  auto events = corundum::dialogue::execute_actions(actions, flags);

  CHECK(flags["gold"] == 5);
  CHECK(flags["paid_innkeeper"] == 1);
  REQUIRE(events.size() == 1);
  CHECK(events[0].name == "play_sound");
  CHECK(events[0].args[0] == "coin");
}

// ── find_node ─────────────────────────────────────────────────────────────────

TEST_CASE("find_node returns correct node for known id") {
  const auto g = make_innkeeper_graph();
  const auto *n0 = corundum::dialogue::find_node(g, "n0");
  REQUIRE(n0 != nullptr);
  CHECK(n0->id == "n0");
  CHECK(g.speaker == "Innkeeper");
  CHECK(n0->type == corundum::dialogue::NodeType::Talk);
}

TEST_CASE("find_node returns nullptr for unknown id") {
  const auto g = make_innkeeper_graph();
  CHECK(corundum::dialogue::find_node(g, "does_not_exist") == nullptr);
  CHECK(corundum::dialogue::find_node(g, "") == nullptr);
}

// ── advance ───────────────────────────────────────────────────────────────────

TEST_CASE("advance on Talk follows next_id") {
  const auto g = make_innkeeper_graph();
  const auto *n0 = corundum::dialogue::find_node(g, "n0");
  REQUIRE(n0 != nullptr);

  const auto *next = corundum::dialogue::advance(g, *n0);
  REQUIRE(next != nullptr);
  CHECK(next->id == "n1");
  CHECK(next->type == corundum::dialogue::NodeType::Choice);
}

TEST_CASE("advance on Talk ignores choice_index") {
  const auto g = make_innkeeper_graph();
  const auto *n0 = corundum::dialogue::find_node(g, "n0");
  REQUIRE(n0 != nullptr);
  CHECK(corundum::dialogue::advance(g, *n0, 99) == corundum::dialogue::advance(g, *n0, 0));
}

TEST_CASE("advance on Event follows next_id") {
  const auto g = make_innkeeper_graph();
  const auto *n_pay = corundum::dialogue::find_node(g, "n_pay");
  REQUIRE(n_pay != nullptr);
  REQUIRE(n_pay->type == corundum::dialogue::NodeType::Event);

  const auto *next = corundum::dialogue::advance(g, *n_pay);
  REQUIRE(next != nullptr);
  CHECK(next->id == "n2");
}

TEST_CASE("advance on Choice follows correct targets") {
  const auto g = make_innkeeper_graph();
  const auto *n1 = corundum::dialogue::find_node(g, "n1");
  REQUIRE(n1 != nullptr);

  CHECK(corundum::dialogue::advance(g, *n1, 0)->id == "n_pay");
  CHECK(corundum::dialogue::advance(g, *n1, 1)->id == "n_already_paid");
  CHECK(corundum::dialogue::advance(g, *n1, 2)->id == "n_bye");
}

TEST_CASE("advance on Choice with out-of-range index returns nullptr") {
  const auto g = make_innkeeper_graph();
  const auto *n1 = corundum::dialogue::find_node(g, "n1");
  REQUIRE(n1 != nullptr);

  CHECK(corundum::dialogue::advance(g, *n1, 3) == nullptr);
  CHECK(corundum::dialogue::advance(g, *n1, -1) == nullptr);
}

TEST_CASE("advance on End always returns nullptr") {
  const auto g = make_innkeeper_graph();
  corundum::dialogue::Node end_node;
  end_node.type = corundum::dialogue::NodeType::End;
  CHECK(corundum::dialogue::advance(g, end_node, 0) == nullptr);
  CHECK(corundum::dialogue::advance(g, end_node, -1) == nullptr);
}

// ── visible_choices ───────────────────────────────────────────────────────────

TEST_CASE("visible_choices: condition gates on expression") {
  corundum::dialogue::Node n;
  n.id = "test";
  n.type = corundum::dialogue::NodeType::Choice;
  n.choices = {
      {.label = "Always.", .target_id = "a"},
      {.label = "Need gold.", .target_id = "b", .condition = *corundum::dialogue::compile("gold >= 5")},
      {.label = "Already paid.", .target_id = "c", .condition = *corundum::dialogue::compile("paid == true")},
  };

  corundum::world::FlagStore flags;
  flags["gold"] = 3;

  // gold < 5 and paid unset
  auto v1 = corundum::dialogue::visible_choices(n, flags, "");
  REQUIRE(v1.size() == 1);
  CHECK(v1[0] == 0);

  // gold >= 5 now
  flags["gold"] = 10;
  auto v2 = corundum::dialogue::visible_choices(n, flags, "");
  REQUIRE(v2.size() == 2);
  CHECK(v2[0] == 0);
  CHECK(v2[1] == 1);

  // paid set
  flags["paid"] = 1;
  auto v3 = corundum::dialogue::visible_choices(n, flags, "");
  REQUIRE(v3.size() == 3);
}

TEST_CASE("visible_choices: Once sequence hides after traversal") {
  using namespace corundum::dialogue;

  Node n;
  n.id = "n1";
  n.type = NodeType::Choice;
  n.choices = {
      {.label = "Once only.", .target_id = "x", .sequence = SequenceMode::Once},
      {.label = "Always.", .target_id = "y"},
  };

  corundum::world::FlagStore flags;
  const auto graph_id = std::string_view("g");

  // Both visible before traversal
  auto before = visible_choices(n, flags, graph_id);
  REQUIRE(before.size() == 2);

  // Simulate taking the Once edge — set its once-flag
  corundum::world::set_flag(flags, once_flag_key(graph_id, n.id, 0));

  auto after = visible_choices(n, flags, graph_id);
  REQUIRE(after.size() == 1);
  CHECK(after[0] == 1); // only the always-visible choice remains
}

TEST_CASE("visible_choices: Cycle sequence rotates per visit") {
  using namespace corundum::dialogue;

  Node n;
  n.id = "cn";
  n.type = NodeType::Choice;
  n.choices = {
      {.label = "Cycle A.", .target_id = "a", .sequence = SequenceMode::Cycle},
      {.label = "Cycle B.", .target_id = "b", .sequence = SequenceMode::Cycle},
      {.label = "Always.", .target_id = "c"},
  };

  corundum::world::FlagStore flags;
  const auto key = visit_flag_key("g", "cn");
  const auto graph_id = std::string_view("g");

  // visit 1: cycle slot 0 → Cycle A visible
  corundum::world::set_flag(flags, key); // visit_count = 1
  auto v1 = visible_choices(n, flags, graph_id);
  // "Always" + Cycle A (slot 0)
  CHECK(v1.size() == 2);
  CHECK(v1[0] == 0); // Cycle A
  CHECK(v1[1] == 2); // Always

  // visit 2: cycle slot 1 → Cycle B visible
  corundum::world::set_flag(flags, key); // visit_count = 2
  auto v2 = visible_choices(n, flags, graph_id);
  CHECK(v2.size() == 2);
  CHECK(v2[0] == 1); // Cycle B
  CHECK(v2[1] == 2); // Always

  // visit 3: wraps back to Cycle A
  corundum::world::set_flag(flags, key); // visit_count = 3
  auto v3 = visible_choices(n, flags, graph_id);
  CHECK(v3[0] == 0); // Cycle A again
}

// ── System / State machine ────────────────────────────────────────────────────

TEST_CASE("dialogue closes automatically on reaching End") {
  corundum::dialogue::Graph g;
  g.graph_id = "end_talk";
  {
    corundum::dialogue::Node n;
    n.id = "n0";
    n.type = corundum::dialogue::NodeType::Talk;
    n.text = "Bye.";
    n.next_id = "end";
    push_node(g, std::move(n));
  }

  corundum::world::FlagStore flags;
  corundum::dialogue::Conversation conversation{g, flags};
  REQUIRE(conversation.is_active());

  // n0 (Talk) → "end": selecting advances onto an End node and closes the dialogue.
  static_cast<void>(conversation.update(select_press()));
  CHECK_FALSE(conversation.is_active());
}

TEST_CASE("Event node fires actions and auto-advances") {
  corundum::dialogue::Graph g;
  g.graph_id = "event_tail";
  {
    corundum::dialogue::Node n;
    n.id = "n_pay";
    n.type = corundum::dialogue::NodeType::Event;
    n.next_id = "n2";
    n.actions = {"play_sound('coin')"};
    push_node(g, std::move(n));
  }
  {
    corundum::dialogue::Node n;
    n.id = "n2";
    n.type = corundum::dialogue::NodeType::Talk;
    n.text = "That'll be 5 gold pieces. Right this way.";
    n.next_id = "end";
    push_node(g, std::move(n));
  }

  corundum::world::FlagStore flags;
  flags["gold"] = 10;

  corundum::dialogue::Conversation conversation{g, flags};
  auto events = conversation.update({});

  // Should have auto-advanced past Event to n2
  CHECK(conversation.is_active());
  CHECK(conversation.current_node_id() == "n2");

  // The play_sound event should have been emitted
  REQUIRE(events.size() == 1);
  CHECK(events[0].name == "play_sound");
  CHECK(events[0].args[0] == "coin");
}

TEST_CASE("Choice selection executes actions") {
  const auto g = make_innkeeper_graph();

  corundum::world::FlagStore flags;
  corundum::dialogue::Conversation conversation{g, flags}; // copies variables: gold=10

  // Advance past n0 (Talk) to n1 (Choice)
  static_cast<void>(conversation.update(select_press()));
  REQUIRE(conversation.current_node_id() == "n1");

  // gold should be 10 from graph variables
  CHECK(flags["gold"] == 10);
  CHECK_FALSE(corundum::world::has_flag(flags, "paid_innkeeper"));

  // Select choice 0 — triggers Event (n_pay) which auto-advances to n2
  static_cast<void>(conversation.update(select_press()));

  // gold -= 5, paid_innkeeper = true should have fired
  CHECK(flags["gold"] == 5);
  CHECK(corundum::world::has_flag(flags, "paid_innkeeper"));

  // Should now be on n2 (Talk), past the Event node
  CHECK(conversation.current_node_id() == "n2");
}

TEST_CASE("is_terminal true only for End nodes") {
  const auto g = make_innkeeper_graph();
  CHECK_FALSE(corundum::dialogue::is_terminal(*corundum::dialogue::find_node(g, "n0")));
  CHECK_FALSE(corundum::dialogue::is_terminal(*corundum::dialogue::find_node(g, "n1")));

  corundum::dialogue::Node end_node;
  end_node.type = corundum::dialogue::NodeType::End;
  CHECK(corundum::dialogue::is_terminal(end_node));
}

// ── Registry ─────────────────────────────────────────────────────────────────

TEST_CASE("registry load_all count matches size with duplicate ids") {
  const auto tmp_dir = std::filesystem::temp_directory_path() / "dialogue_test_dup_count";
  std::filesystem::create_directories(tmp_dir);
  {
    std::ofstream f(tmp_dir / "a.json");
    f << R"({"type":"graph","id":"dup","nodes":[{"id":"n0","type":"talk","text":"A","next":"end"}]})";
  }
  {
    std::ofstream f(tmp_dir / "b.json");
    f << R"({"type":"graph","id":"dup","nodes":[{"id":"n0","type":"talk","text":"B","next":"end"}]})";
  }

  corundum::dialogue::Registry reg;
  int loaded = reg.load_all(tmp_dir);
  CHECK(loaded == 1);
  CHECK(reg.size() == 1);
  std::filesystem::remove_all(tmp_dir);
}

TEST_CASE("eval_condition: quest helper quest_is_started works") {
  corundum::world::FlagStore flags;
  corundum::quest::Registry quests;
  corundum::quest::Quest q;
  q.quest_id = "tq";
  q.name = "TQ";
  q.stages.push_back({"start", 1, false, false, {}});
  q.stages.push_back({"complete", 2, true, false, {}});
  quests.add(std::move(q));

  CHECK_FALSE(*corundum::dialogue::eval_condition("quest_is_started(tq)", flags, &quests));
  flags["quest.tq"] = 1;
  CHECK(*corundum::dialogue::eval_condition("quest_is_started(tq)", flags, &quests));
}

TEST_CASE("eval_condition: quest helper quest_is_at works") {
  corundum::world::FlagStore flags;
  corundum::quest::Registry quests;
  corundum::quest::Quest q;
  q.quest_id = "tq";
  q.name = "TQ";
  q.stages.push_back({"start", 1, false, false, {}});
  q.stages.push_back({"complete", 2, true, false, {}});
  quests.add(std::move(q));

  flags["quest.tq"] = 1;
  CHECK(*corundum::dialogue::eval_condition("quest_is_at(tq, start)", flags, &quests));
  CHECK_FALSE(*corundum::dialogue::eval_condition("quest_is_at(tq, complete)", flags, &quests));
}

TEST_CASE("eval_condition: quest helper quest_is_resolved works") {
  corundum::world::FlagStore flags;
  corundum::quest::Registry quests;
  corundum::quest::Quest q;
  q.quest_id = "tq";
  q.name = "TQ";
  q.stages.push_back({"start", 1, false, false, {}});
  q.stages.push_back({"complete", 2, true, false, {}});
  quests.add(std::move(q));

  flags["quest.tq"] = 2;
  CHECK(*corundum::dialogue::eval_condition("quest_is_resolved(tq)", flags, &quests));
  CHECK_FALSE(*corundum::dialogue::eval_condition("quest_is_resolved(unknown)", flags, &quests));
}

TEST_CASE("eval_condition: old quest helper names error") {
  corundum::world::FlagStore flags;
  corundum::quest::Registry quests;
  auto res = corundum::dialogue::eval_condition("quest_started(tq)", flags, &quests);
  CHECK_FALSE(res.has_value());
  CHECK(res.error().message.find("unknown quest helper") != std::string::npos);
}

TEST_CASE("eval_condition: quest helper with null registry parses and evaluates false") {
  // Defends against the parse_quest_helper null-registry early-return that used to
  // leave the '(' unconsumed — the parser then threw "unexpected token: (" on the
  // closing expression end. After the fix, a null registry is safe: the call parses,
  // the registry lookups return nullptr, and the helpers evaluate to false.
  corundum::world::FlagStore flags;
  auto r1 = corundum::dialogue::eval_condition("!quest_is_resolved(ember)", flags);
  REQUIRE(r1.has_value());
  CHECK(*r1 == true); // false under ! → true

  auto r2 = corundum::dialogue::eval_condition("quest_is_at(ember, done)", flags);
  REQUIRE(r2.has_value());
  CHECK(*r2 == false);

  auto r3 = corundum::dialogue::eval_condition("quest_is_resolved(ember)", flags);
  REQUIRE(r3.has_value());
  CHECK(*r3 == false);

  auto r4 = corundum::dialogue::eval_condition("quest_is_failed(ember)", flags);
  REQUIRE(r4.has_value());
  CHECK(*r4 == false);
}

TEST_CASE("eval_condition: quest_is_started works without a registry") {
  // quest_is_started is a flag-only check (no registry lookup) — it must keep
  // working when the registry is absent. This guards render paths that thread
  // a non-null registry but also covers the null case for completeness.
  corundum::world::FlagStore flags;
  flags["quest.ember"] = 1;

  auto with = corundum::dialogue::eval_condition("quest_is_started(ember)", flags, nullptr);
  REQUIRE(with.has_value());
  CHECK(*with == true);

  corundum::world::FlagStore empty_flags;
  auto without = corundum::dialogue::eval_condition("quest_is_started(ember)", empty_flags, nullptr);
  REQUIRE(without.has_value());
  CHECK(*without == false);
}

TEST_CASE("eval_condition: has_item / item_count / rep helpers") {
  corundum::world::FlagStore flags;
  flags["item.hammer"] = 2;
  flags["rep.village"] = 5;

  // has_item — truthy when count > 0
  CHECK(*corundum::dialogue::eval_condition("has_item(hammer)", flags) == true);
  CHECK(*corundum::dialogue::eval_condition("has_item(sword)", flags) == false);

  // item_count — raw count, usable in comparisons
  CHECK(*corundum::dialogue::eval_condition("item_count(hammer) >= 2", flags) == true);
  CHECK(*corundum::dialogue::eval_condition("item_count(hammer) >= 3", flags) == false);
  CHECK(*corundum::dialogue::eval_condition("item_count(hammer) == 2", flags) == true);

  // rep — raw count, usable in comparisons
  CHECK(*corundum::dialogue::eval_condition("rep(village) >= 5", flags) == true);
  CHECK(*corundum::dialogue::eval_condition("rep(village) > 5", flags) == false);
  CHECK(*corundum::dialogue::eval_condition("rep(other) >= 1", flags) == false);

  // compound conditions
  CHECK(*corundum::dialogue::eval_condition("has_item(hammer) && rep(village) >= 3", flags) == true);
  CHECK(*corundum::dialogue::eval_condition("has_item(sword) || rep(village) >= 3", flags) == true);
}

TEST_CASE("visible_choices: quest-gated choice hidden (not errored) when registry absent") {
  // Render path used to call visible_choices with a null quest::Registry*. Without
  // the parse_quest_helper fix the condition would parse-error and the choice would
  // be hidden with a stderr message; the test now expects the same hide-but-no-error
  // outcome, just without the parse failure.
  using namespace corundum::dialogue;

  Node n;
  n.id = "gate";
  n.type = NodeType::Choice;
  n.choices = {
      {.label = "Always.", .target_id = "a"},
      {.label = "Gated by quest stage.", .target_id = "b", .condition = *compile("quest_is_at(ember, done)")},
  };

  corundum::world::FlagStore flags;

  const auto visible = visible_choices(n, flags, "any_graph");
  REQUIRE(visible.size() == 1);
  CHECK(visible[0] == 0);
}

TEST_CASE("visible_choices: quest-gated choice shown when registry present and stage matches") {
  // Mirrors the gameplay/system path: a populated registry + matching quest flag
  // reveals the gated choice. Guards that the render-side threading of quests
  // reaches eval_condition the same way the system side already does.
  using namespace corundum::dialogue;

  Node n;
  n.id = "gate";
  n.type = NodeType::Choice;
  n.choices = {
      {.label = "Always.", .target_id = "a"},
      {.label = "Gated by quest stage.", .target_id = "b", .condition = *compile("quest_is_at(ember, done)")},
  };

  corundum::world::FlagStore flags;
  corundum::quest::Registry quests;
  corundum::quest::Quest q;
  q.quest_id = "ember";
  q.name = "Ember";
  q.stages.push_back({"start", 1, false, false, {}});
  q.stages.push_back({"done", 2, true, false, {}});
  quests.add(std::move(q));

  // Without the matching flag the gated choice stays hidden.
  auto v1 = visible_choices(n, flags, "any_graph", &quests);
  REQUIRE(v1.size() == 1);
  CHECK(v1[0] == 0);

  // quest.ember = 2 matches stage "done" (sequence 2) — gated choice appears.
  flags["quest.ember"] = 2;
  auto v2 = visible_choices(n, flags, "any_graph", &quests);
  REQUIRE(v2.size() == 2);
  CHECK(v2[0] == 0);
  CHECK(v2[1] == 1);
}

// ── Loader ────────────────────────────────────────────────────────────────────

TEST_CASE("load_graph parses innkeeper.json correctly") {
  const auto result = corundum::dialogue::load_graph("tests/fixtures/innkeeper.json");
  REQUIRE(result.has_value());
  const auto &g = *result;

  CHECK(g.graph_id == "innkeeper_intro");
  CHECK(g.speaker == "Innkeeper");
  CHECK(g.nodes.size() == 6);
  CHECK(g.variables.at("gold") == 10);

  const auto *n0 = corundum::dialogue::find_node(g, "n0");
  REQUIRE(n0 != nullptr);
  CHECK(n0->type == corundum::dialogue::NodeType::Talk);
  CHECK(n0->next_id == "n1");

  const auto *n1 = corundum::dialogue::find_node(g, "n1");
  REQUIRE(n1 != nullptr);
  CHECK(n1->type == corundum::dialogue::NodeType::Choice);
  CHECK(n1->choices.size() == 3);
  CHECK(n1->choices[0].condition->source() == "gold >= 5 && !paid_innkeeper");
  CHECK(n1->choices[0].actions[0] == "gold -= 5");
  CHECK(n1->choices[0].sequence == corundum::dialogue::SequenceMode::Once);
  CHECK(n1->choices[1].condition->source() == "paid_innkeeper == true");

  const auto *n_pay = corundum::dialogue::find_node(g, "n_pay");
  REQUIRE(n_pay != nullptr);
  CHECK(n_pay->type == corundum::dialogue::NodeType::Event);
  CHECK(n_pay->actions[0] == "play_sound('coin')");
  CHECK(n_pay->next_id == "n2");
}

TEST_CASE("load_graph accepts type \"dialogue\" as an alias for \"graph\"") {
  const auto result = corundum::dialogue::load_graph("tests/fixtures/type_alias.json");
  REQUIRE(result.has_value());
  CHECK(result->graph_id == "alias_test");
  CHECK(result->nodes.size() == 1);
}

TEST_CASE("load_graph returns error for missing file") {
  const auto result = corundum::dialogue::load_graph("no_such_file.json");
  CHECK_FALSE(result.has_value());
  CHECK_FALSE(result.error().empty());
}

TEST_CASE("load_graph rejects a dialogue with a malformed condition") {
  const std::string tmp = "tests/fixtures/_test_bad_condition.json";
  {
    std::ofstream f(tmp);
    f << R"({"type":"graph","id":"bad_cond","nodes":[
      {"id":"n0","type":"choice","choices":[
        {"label":"L","target":"end","condition":"gold >="}
      ]}
    ]})";
  }
  const auto result = corundum::dialogue::load_graph(tmp);
  CHECK_FALSE(result.has_value());
  CHECK(result.error().find("condition invalid") != std::string::npos);
  std::filesystem::remove(tmp);
}

TEST_CASE("validate_quest_refs: give_item/take_item unknown item produces error") {
  using namespace corundum::dialogue;

  Graph g;
  g.graph_id = "smith";
  g.speaker = "Osric";
  Node n;
  n.id = "n0";
  n.type = NodeType::Event;
  n.next_id = "end";
  n.actions = {"give_item('hammer', 1)", "take_item('salt', 1)", "give_item('known_item', 1)"};
  g.nodes.push_back(std::move(n));

  corundum::item::Registry items;
  corundum::item::Item item;
  item.id = "known_item";
  item.name = "Known";
  items.add(std::move(item));

  const auto errors = validate_quest_refs(g, {}, &items);
  REQUIRE(errors.size() == 2);
  CHECK(errors[0].find("give_item") != std::string::npos);
  CHECK(errors[0].find("hammer") != std::string::npos);
  CHECK(errors[1].find("take_item") != std::string::npos);
  CHECK(errors[1].find("salt") != std::string::npos);
}

TEST_CASE("validate_quest_refs: null items registry skips item checks") {
  using namespace corundum::dialogue;

  Graph g;
  g.graph_id = "smith";
  g.speaker = "Osric";
  Node n;
  n.id = "n0";
  n.type = NodeType::Event;
  n.next_id = "end";
  n.actions = {"give_item('hammer', 1)"};
  g.nodes.push_back(std::move(n));

  // No item registry (loom) — item references are not validated.
  const auto errors = validate_quest_refs(g, {});
  CHECK(errors.empty());
}

TEST_CASE("validate_condition_quest_refs flags an unknown quest") {
  using namespace corundum::dialogue;

  Graph g;
  g.graph_id = "gate";
  Node n;
  n.id = "n0";
  n.type = NodeType::Choice;
  n.choices = {
      {.label = "Always.", .target_id = "a"},
      {.label = "Gated.", .target_id = "b", .condition = *compile("quest_is_at(missing, nope)")},
  };
  g.nodes.push_back(std::move(n));

  corundum::quest::Registry quests;
  corundum::quest::Quest q;
  q.quest_id = "ember";
  q.name = "Ember";
  q.stages.push_back({"start", 1, false, false, {}});
  quests.add(std::move(q));

  const auto errors = validate_condition_quest_refs(g, quests);
  REQUIRE(errors.size() == 1);
  CHECK(errors[0].find("unknown quest 'missing'") != std::string::npos);
}

TEST_CASE("validate_condition_quest_refs flags an unknown stage for a known quest") {
  using namespace corundum::dialogue;

  Graph g;
  g.graph_id = "gate";
  Node n;
  n.id = "n0";
  n.type = NodeType::Choice;
  n.choices = {
      {.label = "Gated.", .target_id = "b", .condition = *compile("quest_is_at(ember, missing_stage)")},
  };
  g.nodes.push_back(std::move(n));

  corundum::quest::Registry quests;
  corundum::quest::Quest q;
  q.quest_id = "ember";
  q.name = "Ember";
  q.stages.push_back({"start", 1, false, false, {}});
  quests.add(std::move(q));

  const auto errors = validate_condition_quest_refs(g, quests);
  REQUIRE(errors.size() == 1);
  CHECK(errors[0].find("unknown stage 'missing_stage'") != std::string::npos);
}

// ── Divert: goto_graph / return_graph ────────────────────────────────────────

namespace {

  using corundum::dialogue::Graph;
  using corundum::dialogue::Node;
  using corundum::dialogue::NodeType;

  /// Hub graph: a0 (Talk) → a_divert (Event goto_graph) → a1 (Talk) → end.
  /// The resume point after return_graph() is a1 (the divert node's successor).
  Graph make_hub_graph(const std::string &spoke, const std::string &spoke_entry) {
    Graph g;
    g.graph_id = "hub";
    g.speaker = "Hub";
    {
      Node n;
      n.id = "a0";
      n.type = NodeType::Talk;
      n.text = "Hello.";
      n.next_id = "a_divert";
      push_node(g, std::move(n));
    }
    {
      Node n;
      n.id = "a_divert";
      n.type = NodeType::Event;
      n.actions = {std::format("goto_graph('{}', '{}')", spoke, spoke_entry)};
      n.next_id = "a1";
      push_node(g, std::move(n));
    }
    {
      Node n;
      n.id = "a1";
      n.type = NodeType::Talk;
      n.text = "Back in the hub.";
      n.next_id = "end";
      push_node(g, std::move(n));
    }
    return g;
  }

  /// Spoke graph: b0 (Talk) → b_ret (Event return_graph) → end.
  Graph make_leaf_graph(const std::string &id, const std::string &entry) {
    Graph g;
    g.graph_id = id;
    g.speaker = id;
    {
      Node n;
      n.id = entry;
      n.type = NodeType::Talk;
      n.text = "Spoke.";
      n.next_id = "b_ret";
      push_node(g, std::move(n));
    }
    {
      Node n;
      n.id = "b_ret";
      n.type = NodeType::Event;
      n.actions = {"return_graph()"};
      n.next_id = "end";
      push_node(g, std::move(n));
    }
    return g;
  }

  /// Mid graph: b0 (Talk) → b_divert (Event goto_graph leaf) → b1 (Talk) → b_ret (return).
  Graph make_mid_graph() {
    Graph g;
    g.graph_id = "mid";
    g.speaker = "Mid";
    {
      Node n;
      n.id = "b0";
      n.type = NodeType::Talk;
      n.text = "Mid.";
      n.next_id = "b_divert";
      push_node(g, std::move(n));
    }
    {
      Node n;
      n.id = "b_divert";
      n.type = NodeType::Event;
      n.actions = {"goto_graph('leaf', 'c0')"};
      n.next_id = "b1";
      push_node(g, std::move(n));
    }
    {
      Node n;
      n.id = "b1";
      n.type = NodeType::Talk;
      n.text = "Mid again.";
      n.next_id = "b_ret";
      push_node(g, std::move(n));
    }
    {
      Node n;
      n.id = "b_ret";
      n.type = NodeType::Event;
      n.actions = {"return_graph()"};
      n.next_id = "end";
      push_node(g, std::move(n));
    }
    return g;
  }

} // namespace

TEST_CASE("divert: goto_graph A→B runs B from its first node") {
  using namespace corundum::dialogue;

  corundum::dialogue::Registry graphs;
  graphs.add(make_hub_graph("spoke", "b0"));
  graphs.add(make_leaf_graph("spoke", "b0"));

  corundum::world::FlagStore flags;
  Conversation conversation{*graphs.find("hub"), flags, nullptr, &graphs, ""};
  REQUIRE(conversation.is_active());
  CHECK(conversation.current_node_id() == "a0");

  // Advancing a0 lands on a_divert (Event), whose chain fires the divert in the
  // same call — the goto_graph is intercepted before it reaches the engine queue.
  const auto events = conversation.update(select_press());
  CHECK(events.empty());
  REQUIRE(conversation.is_active());
  CHECK(conversation.graph_id() == "spoke");
  CHECK(conversation.current_node_id() == "b0");
  REQUIRE(conversation.call_stack_depth() == 1);
  CHECK(conversation.resume_graph_id() == "hub");
  CHECK(conversation.resume_node_id() == "a1"); // resume where a_divert would have gone
}

TEST_CASE("divert: return_graph pops back to the pushed hub node") {
  using namespace corundum::dialogue;

  corundum::dialogue::Registry graphs;
  graphs.add(make_hub_graph("spoke", "b0"));
  graphs.add(make_leaf_graph("spoke", "b0"));

  corundum::world::FlagStore flags;
  Conversation conversation{*graphs.find("hub"), flags, nullptr, &graphs, ""};
  static_cast<void>(conversation.update(select_press()));
  REQUIRE(conversation.graph_id() == "spoke");
  REQUIRE(conversation.current_node_id() == "b0");

  // Selecting b0 advances to b_ret (Event), which immediately returns the stack.
  const auto events = conversation.update(select_press());
  CHECK(events.empty()); // return_graph was intercepted
  REQUIRE(conversation.is_active());
  CHECK(conversation.graph_id() == "hub");
  CHECK(conversation.current_node_id() == "a1");
  CHECK(conversation.call_stack_depth() == 0);
}

TEST_CASE("divert: nested A→B→C unwinds correctly") {
  using namespace corundum::dialogue;

  // A (hub) diverts to B; B diverts to C; C returns; B returns; A finishes.
  corundum::dialogue::Registry graphs;
  graphs.add(make_hub_graph("mid", "b0"));
  graphs.add(make_mid_graph());
  graphs.add(make_leaf_graph("leaf", "c0"));

  corundum::world::FlagStore flags;
  Conversation conversation{*graphs.find("hub"), flags, nullptr, &graphs, ""};

  // A → B.
  static_cast<void>(conversation.update(select_press()));
  REQUIRE(conversation.graph_id() == "mid");
  CHECK(conversation.call_stack_depth() == 1);

  // B → C.
  static_cast<void>(conversation.update(select_press()));
  REQUIRE(conversation.graph_id() == "leaf");
  CHECK(conversation.call_stack_depth() == 2);

  // C returns → B's b1.
  static_cast<void>(conversation.update(select_press()));
  REQUIRE(conversation.graph_id() == "mid");
  CHECK(conversation.current_node_id() == "b1");
  CHECK(conversation.call_stack_depth() == 1);

  // B returns → A's a1.
  static_cast<void>(conversation.update(select_press()));
  REQUIRE(conversation.graph_id() == "hub");
  CHECK(conversation.current_node_id() == "a1");
  CHECK(conversation.call_stack_depth() == 0);

  // A finishes normally.
  static_cast<void>(conversation.update(select_press()));
  CHECK_FALSE(conversation.is_active());
}

TEST_CASE("divert: per-graph visit counts stay independent") {
  using namespace corundum::dialogue;

  corundum::dialogue::Registry graphs;
  graphs.add(make_hub_graph("mid", "b0"));
  graphs.add(make_mid_graph());
  graphs.add(make_leaf_graph("leaf", "c0"));

  corundum::world::FlagStore flags;
  Conversation conversation{*graphs.find("hub"), flags, nullptr, &graphs, ""};
  static_cast<void>(conversation.update(select_press()));
  static_cast<void>(conversation.update(select_press()));
  static_cast<void>(conversation.update(select_press()));
  static_cast<void>(conversation.update(select_press()));
  static_cast<void>(conversation.update(select_press()));
  CHECK_FALSE(conversation.is_active());

  // Each graph's node visits live under its own graph-id key, untouched by the others.
  CHECK(corundum::world::visit_count(flags, visit_flag_key("hub", "a0")) == 1);
  CHECK(corundum::world::visit_count(flags, visit_flag_key("hub", "a1")) == 1);
  CHECK(corundum::world::visit_count(flags, visit_flag_key("mid", "b0")) == 1);
  CHECK(corundum::world::visit_count(flags, visit_flag_key("leaf", "c0")) == 1);
  CHECK(corundum::world::visit_count(flags, visit_flag_key("hub", "b0")) == 0); // no cross-graph bleed
}

TEST_CASE("divert: return_graph on an empty stack ends the dialogue") {
  using namespace corundum::dialogue;

  Graph g;
  g.graph_id = "lone";
  g.speaker = "Lone";
  {
    Node n;
    n.id = "a0";
    n.type = NodeType::Talk;
    n.text = "Hello.";
    n.next_id = "a_ret";
    push_node(g, std::move(n));
  }
  {
    Node n;
    n.id = "a_ret";
    n.type = NodeType::Event;
    n.actions = {"return_graph()"};
    n.next_id = "end";
    push_node(g, std::move(n));
  }

  corundum::world::FlagStore flags;
  Conversation conversation{g, flags};
  const auto events = conversation.update(select_press());
  CHECK(events.empty());
  CHECK_FALSE(conversation.is_active());
}

TEST_CASE("divert: validate_quest_refs flags a missing goto_graph target") {
  using namespace corundum::dialogue;

  Graph g;
  g.graph_id = "sender";
  Node n;
  n.id = "n0";
  n.type = NodeType::Event;
  n.next_id = "end";
  n.actions = {"goto_graph('missing_graph', 'n0')"};
  g.nodes.push_back(std::move(n));

  // A null graphs registry skips divert checks.
  const auto skipped = validate_quest_refs(g, {}, nullptr, nullptr);
  CHECK(skipped.empty());

  // With a registry threaded, the unknown graph is reported.
  const corundum::dialogue::Registry empty_registry;
  const auto flagged = validate_quest_refs(g, {}, nullptr, &empty_registry);
  REQUIRE(flagged.size() == 1);
  CHECK(flagged[0].find("unknown graph 'missing_graph'") != std::string::npos);
}

TEST_CASE("divert: validate_quest_refs flags a missing node in an existing graph") {
  using namespace corundum::dialogue;

  Graph sender;
  sender.graph_id = "sender";
  Node n;
  n.id = "n0";
  n.type = NodeType::Event;
  n.next_id = "end";
  n.actions = {"goto_graph('target', 'nope')"};
  sender.nodes.push_back(std::move(n));

  Graph target;
  target.graph_id = "target";
  {
    Node tn;
    tn.id = "n0";
    tn.type = NodeType::Talk;
    tn.text = "Target.";
    tn.next_id = "end";
    push_node(target, std::move(tn));
  }

  corundum::dialogue::Registry graphs;
  graphs.add(std::move(target));

  const auto errors = validate_quest_refs(sender, {}, nullptr, &graphs);
  REQUIRE(errors.size() == 1);
  CHECK(errors[0].find("unknown node 'nope' in 'target'") != std::string::npos);
}

// ── actor_id ───────────────────────────────────────────────────────────────────

TEST_CASE("actor_id: loads from JSON and round-trips through serialize") {
  const auto result = corundum::dialogue::load_graph("tests/fixtures/actor_dialogue.json");
  REQUIRE(result.has_value());
  CHECK(result->actor_id == "brann");

  const auto j = corundum::dialogue::serialize(*result);
  CHECK(j["actor_id"].get<std::string>() == "brann");

  const auto tmp = std::filesystem::path("tests/fixtures/tmp_actor_dialogue.json");
  auto write_result = corundum::core::write_json(tmp, j);
  REQUIRE(write_result.has_value());

  const auto reloaded = corundum::dialogue::load_graph(tmp.string());
  REQUIRE(reloaded.has_value());
  CHECK(reloaded->actor_id == "brann");

  std::filesystem::remove(tmp);
}

TEST_CASE("actor_id: absent field leaves actor_id empty and serialize omits it") {
  const auto result = corundum::dialogue::load_graph("tests/fixtures/innkeeper.json");
  REQUIRE(result.has_value());
  CHECK(result->actor_id.empty());

  const auto j = corundum::dialogue::serialize(*result);
  CHECK_FALSE(j.contains("actor_id"));
}

// ── Node-level once ────────────────────────────────────────────────────────────

TEST_CASE("Talk once: line is shown the first time and skipped on revisit") {
  using namespace corundum::dialogue;

  Graph g;
  g.graph_id = "once_talk";
  {
    Node n;
    n.id = "n0";
    n.type = NodeType::Talk;
    n.text = "Only once.";
    n.next_id = "n1";
    n.once = true;
    push_node(g, std::move(n));
  }
  {
    Node n;
    n.id = "n1";
    n.type = NodeType::Talk;
    n.text = "The rest.";
    n.next_id = "end";
    push_node(g, std::move(n));
  }

  corundum::world::FlagStore flags;
  Conversation first{g, flags};
  REQUIRE(first.is_active());
  CHECK(first.current_node_id() == "n0");

  // First visit: no input yet, still waiting on n0.
  static_cast<void>(first.update({}));
  CHECK(first.current_node_id() == "n0");
  CHECK(first.is_active());

  // Select advances past n0 to n1, marking n0 shown.
  static_cast<void>(first.update(select_press()));
  CHECK(first.current_node_id() == "n1");

  // n1 → end closes the dialogue.
  static_cast<void>(first.update(select_press()));
  CHECK_FALSE(first.is_active());

  // Reopen: n0 was already shown, so no input is required to skip it.
  Conversation reopened{g, flags};
  REQUIRE(reopened.current_node_id() == "n0");
  static_cast<void>(reopened.update({}));
  CHECK(reopened.current_node_id() == "n1");
  CHECK(reopened.is_active());
}

TEST_CASE("Talk once: loads from JSON and round-trips through serialize") {
  const std::string tmp = "tests/fixtures/_test_once_talk.json";
  {
    std::ofstream f(tmp);
    f << R"({"type":"graph","id":"once_json","nodes":[
      {"id":"n0","type":"talk","text":"Once.","next":"end","once":true}
    ]})";
  }
  const auto result = corundum::dialogue::load_graph(tmp);
  REQUIRE(result.has_value());
  const auto *n0 = corundum::dialogue::find_node(*result, "n0");
  REQUIRE(n0 != nullptr);
  CHECK(n0->once == true);

  const auto j = corundum::dialogue::serialize(*result);
  CHECK(j["nodes"][0]["once"].get<bool>() == true);

  const auto tmp2 = std::filesystem::path("tests/fixtures/tmp_once_talk.json");
  auto write_result = corundum::core::write_json(tmp2, j);
  REQUIRE(write_result.has_value());

  const auto reloaded = corundum::dialogue::load_graph(tmp2.string());
  REQUIRE(reloaded.has_value());
  CHECK(corundum::dialogue::find_node(*reloaded, "n0")->once == true);

  std::filesystem::remove(tmp);
  std::filesystem::remove(tmp2);
}

TEST_CASE("Talk once: absent field defaults to false and serialize omits it") {
  const auto result = corundum::dialogue::load_graph("tests/fixtures/innkeeper.json");
  REQUIRE(result.has_value());
  CHECK_FALSE(corundum::dialogue::find_node(*result, "n0")->once);

  const auto j = corundum::dialogue::serialize(*result);
  CHECK_FALSE(j["nodes"][0].contains("once"));
}

// ── Round-trip ────────────────────────────────────────────────────────────────

TEST_CASE("dialogue serialize round-trips through load_graph") {
  const auto result = corundum::dialogue::load_graph("tests/fixtures/innkeeper.json");
  REQUIRE(result.has_value());
  const auto &g = *result;

  const auto j = corundum::dialogue::serialize(g);

  const auto tmp = std::filesystem::path("tests/fixtures/tmp_innkeeper.json");
  auto write_result = corundum::core::write_json(tmp, j);
  REQUIRE(write_result.has_value());

  const auto reloaded = corundum::dialogue::load_graph(tmp.string());
  REQUIRE(reloaded.has_value());
  const auto &g2 = *reloaded;

  CHECK(g2.graph_id == g.graph_id);
  CHECK(g2.speaker == g.speaker);
  CHECK(g2.nodes.size() == g.nodes.size());
  CHECK(g2.variables == g.variables);

  const auto *n0 = corundum::dialogue::find_node(g2, "n0");
  REQUIRE(n0 != nullptr);
  CHECK(n0->type == corundum::dialogue::NodeType::Talk);
  CHECK(n0->next_id == "n1");

  const auto *n1 = corundum::dialogue::find_node(g2, "n1");
  REQUIRE(n1 != nullptr);
  CHECK(n1->type == corundum::dialogue::NodeType::Choice);
  CHECK(n1->choices.size() == 3);
  CHECK(n1->choices[0].condition->source() == "gold >= 5 && !paid_innkeeper");
  CHECK(n1->choices[0].actions[0] == "gold -= 5");
  CHECK(n1->choices[0].sequence == corundum::dialogue::SequenceMode::Once);

  const auto *n_pay = corundum::dialogue::find_node(g2, "n_pay");
  REQUIRE(n_pay != nullptr);
  CHECK(n_pay->type == corundum::dialogue::NodeType::Event);
  CHECK(n_pay->actions[0] == "play_sound('coin')");
  CHECK(n_pay->next_id == "n2");

  std::filesystem::remove(tmp);
}
