#include <doctest/doctest.h>

#include <corundum/gameplay/dialogue/action.hpp>
#include <corundum/gameplay/dialogue/dialogue.hpp>
#include <corundum/gameplay/dialogue/expr.hpp>
#include <corundum/gameplay/dialogue/loader.hpp>
#include <corundum/gameplay/dialogue/query.hpp>
#include <corundum/gameplay/dialogue/system.hpp>
#include <corundum/gameplay/flags.hpp>

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
static corundum::gameplay::dialogue::Graph make_innkeeper_graph() {
  using namespace corundum::gameplay::dialogue;

  Graph g;
  g.graph_id = "innkeeper_intro";
  g.variables = {{"gold", 10}};

  auto push = [&](Node n) {
    g.id_to_index[n.id] = g.nodes.size();
    g.nodes.push_back(std::move(n));
  };

  { // n0 — talk
    Node n;
    n.id = "n0";
    n.type = NodeType::Talk;
    n.speaker = "Innkeeper";
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
         .condition = "gold >= 5 && !paid_innkeeper",
         .actions = {"gold -= 5", "paid_innkeeper = true"},
         .sequence = SequenceMode::Once},
        {.label = "I need a room.", .target_id = "n_already_paid", .condition = "paid_innkeeper == true"},
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
    n.speaker = "Innkeeper";
    n.text = "That'll be 5 gold pieces. Right this way.";
    n.next_id = "end";
    push(std::move(n));
  }
  { // n_already_paid — talk
    Node n;
    n.id = "n_already_paid";
    n.type = NodeType::Talk;
    n.speaker = "Innkeeper";
    n.text = "You've already paid. Your room is the last one on the left. Sleep well.";
    n.next_id = "end";
    push(std::move(n));
  }
  { // n_bye — talk
    Node n;
    n.id = "n_bye";
    n.type = NodeType::Talk;
    n.speaker = "Innkeeper";
    n.text = "Safe travels then. Watch the road south.";
    n.next_id = "end";
    push(std::move(n));
  }

  return g;
}

// ── eval_condition ────────────────────────────────────────────────────────────

TEST_CASE("eval_condition: empty expr returns true") {
  corundum::gameplay::FlagStore flags;
  auto res = corundum::gameplay::dialogue::eval_condition("", flags);
  REQUIRE(res.has_value());
  CHECK(*res == true);
}

TEST_CASE("eval_condition: integer literal truthy/falsy") {
  corundum::gameplay::FlagStore flags;
  CHECK(*corundum::gameplay::dialogue::eval_condition("1", flags) == true);
  CHECK(*corundum::gameplay::dialogue::eval_condition("0", flags) == false);
  CHECK(*corundum::gameplay::dialogue::eval_condition("42", flags) == true);
}

TEST_CASE("eval_condition: boolean literals") {
  corundum::gameplay::FlagStore flags;
  CHECK(*corundum::gameplay::dialogue::eval_condition("true", flags) == true);
  CHECK(*corundum::gameplay::dialogue::eval_condition("false", flags) == false);
}

TEST_CASE("eval_condition: identifier resolves from FlagStore") {
  corundum::gameplay::FlagStore flags;
  corundum::gameplay::set_flag(flags, "paid");

  CHECK(*corundum::gameplay::dialogue::eval_condition("paid", flags) == true);
  CHECK(*corundum::gameplay::dialogue::eval_condition("other", flags) == false);
}

TEST_CASE("eval_condition: comparison operators") {
  corundum::gameplay::FlagStore flags;
  flags["gold"] = 10;

  CHECK(*corundum::gameplay::dialogue::eval_condition("gold == 10", flags) == true);
  CHECK(*corundum::gameplay::dialogue::eval_condition("gold != 10", flags) == false);
  CHECK(*corundum::gameplay::dialogue::eval_condition("gold >= 5", flags) == true);
  CHECK(*corundum::gameplay::dialogue::eval_condition("gold > 10", flags) == false);
  CHECK(*corundum::gameplay::dialogue::eval_condition("gold < 11", flags) == true);
  CHECK(*corundum::gameplay::dialogue::eval_condition("gold <= 10", flags) == true);
}

TEST_CASE("eval_condition: boolean operators") {
  corundum::gameplay::FlagStore flags;
  flags["gold"] = 10;

  CHECK(*corundum::gameplay::dialogue::eval_condition("gold >= 5 && gold < 20", flags) == true);
  CHECK(*corundum::gameplay::dialogue::eval_condition("gold > 20 || gold == 10", flags) == true);
  CHECK(*corundum::gameplay::dialogue::eval_condition("!(gold == 10)", flags) == false);
}

TEST_CASE("eval_condition: bool comparison uses truthiness") {
  corundum::gameplay::FlagStore flags;
  corundum::gameplay::set_flag(flags, "paid"); // count = 1
  corundum::gameplay::set_flag(flags, "paid"); // count = 2

  // paid == true should be truthy (count != 0), not strictly equal to 1
  CHECK(*corundum::gameplay::dialogue::eval_condition("paid == true", flags) == true);
  CHECK(*corundum::gameplay::dialogue::eval_condition("paid == false", flags) == false);
}

TEST_CASE("eval_condition: combined innkeeper condition") {
  corundum::gameplay::FlagStore flags;
  flags["gold"] = 10;

  CHECK(*corundum::gameplay::dialogue::eval_condition("gold >= 5 && !paid_innkeeper", flags) == true);

  flags["paid_innkeeper"] = 1;
  CHECK(*corundum::gameplay::dialogue::eval_condition("gold >= 5 && !paid_innkeeper", flags) == false);
}

TEST_CASE("eval_condition: parse error returns ExprError") {
  corundum::gameplay::FlagStore flags;
  auto res = corundum::gameplay::dialogue::eval_condition("gold >=", flags);
  CHECK_FALSE(res.has_value());
  CHECK_FALSE(res.error().message.empty());
}

// ── parse_action ──────────────────────────────────────────────────────────────

TEST_CASE("parse_action: assign integer") {
  auto r = corundum::gameplay::dialogue::parse_action("gold = 5");
  REQUIRE(r.has_value());
  const auto &sa = std::get<corundum::gameplay::dialogue::StateAction>(*r);
  CHECK(sa.var == "gold");
  CHECK(sa.op == corundum::gameplay::dialogue::StateAction::Op::Assign);
  CHECK(sa.value == 5);
}

TEST_CASE("parse_action: assign true/false") {
  auto r_true = corundum::gameplay::dialogue::parse_action("paid = true");
  REQUIRE(r_true.has_value());
  CHECK(std::get<corundum::gameplay::dialogue::StateAction>(*r_true).value == 1);

  auto r_false = corundum::gameplay::dialogue::parse_action("paid = false");
  REQUIRE(r_false.has_value());
  CHECK(std::get<corundum::gameplay::dialogue::StateAction>(*r_false).value == 0);
}

TEST_CASE("parse_action: add and subtract") {
  auto r_add = corundum::gameplay::dialogue::parse_action("gold += 3");
  REQUIRE(r_add.has_value());
  const auto &sa = std::get<corundum::gameplay::dialogue::StateAction>(*r_add);
  CHECK(sa.op == corundum::gameplay::dialogue::StateAction::Op::Add);
  CHECK(sa.value == 3);

  auto r_sub = corundum::gameplay::dialogue::parse_action("gold -= 5");
  REQUIRE(r_sub.has_value());
  CHECK(std::get<corundum::gameplay::dialogue::StateAction>(*r_sub).op ==
        corundum::gameplay::dialogue::StateAction::Op::Sub);
}

TEST_CASE("parse_action: event call") {
  auto r = corundum::gameplay::dialogue::parse_action("play_sound('coin')");
  REQUIRE(r.has_value());
  const auto &ev = std::get<corundum::gameplay::dialogue::EventAction>(*r);
  CHECK(ev.name == "play_sound");
  REQUIRE(ev.args.size() == 1);
  CHECK(ev.args[0] == "coin");
}

TEST_CASE("parse_action: event call with multiple args") {
  auto r = corundum::gameplay::dialogue::parse_action("trigger_event('play_sound', 'inn_door')");
  REQUIRE(r.has_value());
  const auto &ev = std::get<corundum::gameplay::dialogue::EventAction>(*r);
  CHECK(ev.name == "trigger_event");
  REQUIRE(ev.args.size() == 2);
  CHECK(ev.args[0] == "play_sound");
  CHECK(ev.args[1] == "inn_door");
}

TEST_CASE("parse_action: parse error returns ActionError") {
  auto r = corundum::gameplay::dialogue::parse_action("???");
  CHECK_FALSE(r.has_value());
  CHECK_FALSE(r.error().message.empty());
}

// ── execute_actions ───────────────────────────────────────────────────────────

TEST_CASE("execute_actions applies state mutations and returns event actions") {
  corundum::gameplay::FlagStore flags;
  flags["gold"] = 10;

  const std::vector<std::string> actions = {
      "gold -= 5",
      "paid_innkeeper = true",
      "play_sound('coin')",
  };

  auto events = corundum::gameplay::dialogue::execute_actions(actions, flags);

  CHECK(flags["gold"] == 5);
  CHECK(flags["paid_innkeeper"] == 1);
  REQUIRE(events.size() == 1);
  CHECK(events[0].name == "play_sound");
  CHECK(events[0].args[0] == "coin");
}

// ── find_node ─────────────────────────────────────────────────────────────────

TEST_CASE("find_node returns correct node for known id") {
  const auto g = make_innkeeper_graph();
  const auto *n0 = corundum::gameplay::dialogue::find_node(g, "n0");
  REQUIRE(n0 != nullptr);
  CHECK(n0->id == "n0");
  CHECK(n0->speaker == "Innkeeper");
  CHECK(n0->type == corundum::gameplay::dialogue::NodeType::Talk);
}

TEST_CASE("find_node returns nullptr for unknown id") {
  const auto g = make_innkeeper_graph();
  CHECK(corundum::gameplay::dialogue::find_node(g, "does_not_exist") == nullptr);
  CHECK(corundum::gameplay::dialogue::find_node(g, "") == nullptr);
}

// ── advance ───────────────────────────────────────────────────────────────────

TEST_CASE("advance on Talk follows next_id") {
  const auto g = make_innkeeper_graph();
  const auto *n0 = corundum::gameplay::dialogue::find_node(g, "n0");
  REQUIRE(n0 != nullptr);

  const auto *next = corundum::gameplay::dialogue::advance(g, *n0);
  REQUIRE(next != nullptr);
  CHECK(next->id == "n1");
  CHECK(next->type == corundum::gameplay::dialogue::NodeType::Choice);
}

TEST_CASE("advance on Talk ignores choice_index") {
  const auto g = make_innkeeper_graph();
  const auto *n0 = corundum::gameplay::dialogue::find_node(g, "n0");
  REQUIRE(n0 != nullptr);
  CHECK(corundum::gameplay::dialogue::advance(g, *n0, 99) == corundum::gameplay::dialogue::advance(g, *n0, 0));
}

TEST_CASE("advance on Event follows next_id") {
  const auto g = make_innkeeper_graph();
  const auto *n_pay = corundum::gameplay::dialogue::find_node(g, "n_pay");
  REQUIRE(n_pay != nullptr);
  REQUIRE(n_pay->type == corundum::gameplay::dialogue::NodeType::Event);

  const auto *next = corundum::gameplay::dialogue::advance(g, *n_pay);
  REQUIRE(next != nullptr);
  CHECK(next->id == "n2");
}

TEST_CASE("advance on Choice follows correct targets") {
  const auto g = make_innkeeper_graph();
  const auto *n1 = corundum::gameplay::dialogue::find_node(g, "n1");
  REQUIRE(n1 != nullptr);

  CHECK(corundum::gameplay::dialogue::advance(g, *n1, 0)->id == "n_pay");
  CHECK(corundum::gameplay::dialogue::advance(g, *n1, 1)->id == "n_already_paid");
  CHECK(corundum::gameplay::dialogue::advance(g, *n1, 2)->id == "n_bye");
}

TEST_CASE("advance on Choice with out-of-range index returns nullptr") {
  const auto g = make_innkeeper_graph();
  const auto *n1 = corundum::gameplay::dialogue::find_node(g, "n1");
  REQUIRE(n1 != nullptr);

  CHECK(corundum::gameplay::dialogue::advance(g, *n1, 3) == nullptr);
  CHECK(corundum::gameplay::dialogue::advance(g, *n1, -1) == nullptr);
}

TEST_CASE("advance on End always returns nullptr") {
  const auto g = make_innkeeper_graph();
  corundum::gameplay::dialogue::Node end_node;
  end_node.type = corundum::gameplay::dialogue::NodeType::End;
  CHECK(corundum::gameplay::dialogue::advance(g, end_node, 0) == nullptr);
  CHECK(corundum::gameplay::dialogue::advance(g, end_node, -1) == nullptr);
}

// ── visible_choices ───────────────────────────────────────────────────────────

TEST_CASE("visible_choices: condition gates on expression") {
  corundum::gameplay::dialogue::Node n;
  n.id = "test";
  n.type = corundum::gameplay::dialogue::NodeType::Choice;
  n.choices = {
      {.label = "Always.", .target_id = "a"},
      {.label = "Need gold.", .target_id = "b", .condition = "gold >= 5"},
      {.label = "Already paid.", .target_id = "c", .condition = "paid == true"},
  };

  corundum::gameplay::FlagStore flags;
  flags["gold"] = 3;

  // gold < 5 and paid unset
  auto v1 = corundum::gameplay::dialogue::visible_choices(n, flags, "");
  REQUIRE(v1.size() == 1);
  CHECK(v1[0] == 0);

  // gold >= 5 now
  flags["gold"] = 10;
  auto v2 = corundum::gameplay::dialogue::visible_choices(n, flags, "");
  REQUIRE(v2.size() == 2);
  CHECK(v2[0] == 0);
  CHECK(v2[1] == 1);

  // paid set
  flags["paid"] = 1;
  auto v3 = corundum::gameplay::dialogue::visible_choices(n, flags, "");
  REQUIRE(v3.size() == 3);
}

TEST_CASE("visible_choices: Once sequence hides after traversal") {
  using namespace corundum::gameplay::dialogue;

  Node n;
  n.id = "n1";
  n.type = NodeType::Choice;
  n.choices = {
      {.label = "Once only.", .target_id = "x", .sequence = SequenceMode::Once},
      {.label = "Always.", .target_id = "y"},
  };

  corundum::gameplay::FlagStore flags;
  const auto graph_id = std::string_view("g");

  // Both visible before traversal
  auto before = visible_choices(n, flags, graph_id);
  REQUIRE(before.size() == 2);

  // Simulate taking the Once edge — set its once-flag
  corundum::gameplay::set_flag(flags, once_flag_key(graph_id, n.id, 0));

  auto after = visible_choices(n, flags, graph_id);
  REQUIRE(after.size() == 1);
  CHECK(after[0] == 1); // only the always-visible choice remains
}

TEST_CASE("visible_choices: Cycle sequence rotates per visit") {
  using namespace corundum::gameplay::dialogue;

  Node n;
  n.id = "cn";
  n.type = NodeType::Choice;
  n.choices = {
      {.label = "Cycle A.", .target_id = "a", .sequence = SequenceMode::Cycle},
      {.label = "Cycle B.", .target_id = "b", .sequence = SequenceMode::Cycle},
      {.label = "Always.", .target_id = "c"},
  };

  corundum::gameplay::FlagStore flags;
  const auto key = visit_flag_key("g", "cn");
  const auto graph_id = std::string_view("g");

  // visit 1: cycle slot 0 → Cycle A visible
  corundum::gameplay::set_flag(flags, key); // visit_count = 1
  auto v1 = visible_choices(n, flags, graph_id);
  // "Always" + Cycle A (slot 0)
  CHECK(v1.size() == 2);
  CHECK(v1[0] == 0); // Cycle A
  CHECK(v1[1] == 2); // Always

  // visit 2: cycle slot 1 → Cycle B visible
  corundum::gameplay::set_flag(flags, key); // visit_count = 2
  auto v2 = visible_choices(n, flags, graph_id);
  CHECK(v2.size() == 2);
  CHECK(v2[0] == 1); // Cycle B
  CHECK(v2[1] == 2); // Always

  // visit 3: wraps back to Cycle A
  corundum::gameplay::set_flag(flags, key); // visit_count = 3
  auto v3 = visible_choices(n, flags, graph_id);
  CHECK(v3[0] == 0); // Cycle A again
}

// ── System / State machine ────────────────────────────────────────────────────

TEST_CASE("dialogue closes automatically on reaching End") {
  const auto g = make_innkeeper_graph();

  corundum::gameplay::dialogue::State state;
  corundum::gameplay::FlagStore flags;
  corundum::gameplay::dialogue::start(state, g, flags);
  REQUIRE(state.active);

  // Advance directly to n2 (Talk → "end") and press Select
  state.current_id = "n2";
  corundum::input::PressedActions select{};
  select.actions[0] = corundum::input::Action::Select;
  select.count = 1;
  static_cast<void>(corundum::gameplay::dialogue::system(state, select, flags));
  CHECK_FALSE(state.active);
}

TEST_CASE("Event node fires actions and auto-advances") {
  const auto g = make_innkeeper_graph();

  corundum::gameplay::dialogue::State state;
  corundum::gameplay::FlagStore flags;
  flags["gold"] = 10;

  corundum::gameplay::dialogue::start(state, g, flags);
  state.current_id = "n_pay";

  auto events = corundum::gameplay::dialogue::system(state, {}, flags);

  // Should have auto-advanced past Event to n2
  CHECK(state.active);
  CHECK(state.current_id == "n2");

  // The play_sound event should have been emitted
  REQUIRE(events.size() == 1);
  CHECK(events[0].name == "play_sound");
  CHECK(events[0].args[0] == "coin");
}

TEST_CASE("Choice selection executes actions") {
  const auto g = make_innkeeper_graph();

  corundum::gameplay::dialogue::State state;
  corundum::gameplay::FlagStore flags;
  corundum::gameplay::dialogue::start(state, g, flags); // copies variables: gold=10

  // Advance past n0 (Talk) to n1 (Choice)
  corundum::input::PressedActions select{};
  select.actions[0] = corundum::input::Action::Select;
  select.count = 1;
  static_cast<void>(corundum::gameplay::dialogue::system(state, select, flags));
  REQUIRE(state.current_id == "n1");

  // gold should be 10 from graph variables
  CHECK(flags["gold"] == 10);
  CHECK_FALSE(corundum::gameplay::has_flag(flags, "paid_innkeeper"));

  // Select choice 0 — triggers Event (n_pay) which auto-advances to n2
  static_cast<void>(corundum::gameplay::dialogue::system(state, select, flags));

  // gold -= 5, paid_innkeeper = true should have fired
  CHECK(flags["gold"] == 5);
  CHECK(corundum::gameplay::has_flag(flags, "paid_innkeeper"));

  // Should now be on n2 (Talk), past the Event node
  CHECK(state.current_id == "n2");
}

TEST_CASE("is_terminal true only for End nodes") {
  const auto g = make_innkeeper_graph();
  CHECK_FALSE(corundum::gameplay::dialogue::is_terminal(*corundum::gameplay::dialogue::find_node(g, "n0")));
  CHECK_FALSE(corundum::gameplay::dialogue::is_terminal(*corundum::gameplay::dialogue::find_node(g, "n1")));

  corundum::gameplay::dialogue::Node end_node;
  end_node.type = corundum::gameplay::dialogue::NodeType::End;
  CHECK(corundum::gameplay::dialogue::is_terminal(end_node));
}

// ── Loader ────────────────────────────────────────────────────────────────────

TEST_CASE("load_graph parses innkeeper.json correctly") {
  const auto result = corundum::gameplay::dialogue::load_graph("tests/fixtures/innkeeper.json");
  REQUIRE(result.has_value());
  const auto &g = *result;

  CHECK(g.graph_id == "innkeeper_intro");
  CHECK(g.nodes.size() == 6);
  CHECK(g.variables.at("gold") == 10);

  const auto *n0 = corundum::gameplay::dialogue::find_node(g, "n0");
  REQUIRE(n0 != nullptr);
  CHECK(n0->type == corundum::gameplay::dialogue::NodeType::Talk);
  CHECK(n0->speaker == "Innkeeper");
  CHECK(n0->next_id == "n1");

  const auto *n1 = corundum::gameplay::dialogue::find_node(g, "n1");
  REQUIRE(n1 != nullptr);
  CHECK(n1->type == corundum::gameplay::dialogue::NodeType::Choice);
  CHECK(n1->choices.size() == 3);
  CHECK(n1->choices[0].condition == "gold >= 5 && !paid_innkeeper");
  CHECK(n1->choices[0].actions[0] == "gold -= 5");
  CHECK(n1->choices[0].sequence == corundum::gameplay::dialogue::SequenceMode::Once);
  CHECK(n1->choices[1].condition == "paid_innkeeper == true");

  const auto *n_pay = corundum::gameplay::dialogue::find_node(g, "n_pay");
  REQUIRE(n_pay != nullptr);
  CHECK(n_pay->type == corundum::gameplay::dialogue::NodeType::Event);
  CHECK(n_pay->actions[0] == "play_sound('coin')");
  CHECK(n_pay->next_id == "n2");
}

TEST_CASE("load_graph returns error for missing file") {
  const auto result = corundum::gameplay::dialogue::load_graph("no_such_file.json");
  CHECK_FALSE(result.has_value());
  CHECK_FALSE(result.error().empty());
}
