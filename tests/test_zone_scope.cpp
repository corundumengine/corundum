#include <doctest/doctest.h>

#include <corundum/dialogue/action.hpp>
#include <corundum/dialogue/compiled_expr.hpp>
#include <corundum/dialogue/expr.hpp>
#include <corundum/dialogue/query.hpp>
#include <corundum/world/flags.hpp>

#include <string>
#include <vector>

using corundum::world::FlagStore;

// ── local.<key> write scoping ────────────────────────────────────────────────

TEST_CASE("zone scope: local.<key> action writes resolve to zone.<zone_id>.<key>") {
  FlagStore flags;
  const std::vector<std::string> actions = {"local.x = 1", "local.counter += 2", "local.debt -= 3"};
  const auto events = corundum::dialogue::execute_actions(actions, flags, "cave");
  CHECK(events.empty());

  CHECK(flags["zone.cave.x"] == 1);
  CHECK(flags["zone.cave.counter"] == 2);
  CHECK(flags["zone.cave.debt"] == -3);
  // No unscoped or other-zone keys are created.
  CHECK_FALSE(flags.contains("local.x"));
  CHECK_FALSE(flags.contains("zone.village.x"));
}

TEST_CASE("zone scope: bare keys are untouched by scoping") {
  FlagStore flags;
  const std::vector<std::string> actions = {"gold = 5"};
  const auto events = corundum::dialogue::execute_actions(actions, flags, "cave");
  CHECK(events.empty());

  CHECK(flags["gold"] == 5);
  CHECK_FALSE(flags.contains("zone.cave.gold"));
  CHECK(*corundum::dialogue::eval_condition("gold == 5", flags));
}

// ── local.<key> read scoping ─────────────────────────────────────────────────

TEST_CASE("zone scope: local.<key> condition reads resolve per zone_id") {
  FlagStore flags;
  flags["zone.cave.x"] = 1;

  const auto in_cave = corundum::dialogue::compile("local.x == 1");
  REQUIRE(in_cave.has_value());
  CHECK(corundum::dialogue::evaluate(*in_cave, flags, nullptr, "cave"));

  // The same expression in a different zone sees 0.
  const auto in_village = corundum::dialogue::compile("local.x == 1");
  REQUIRE(in_village.has_value());
  CHECK_FALSE(corundum::dialogue::evaluate(*in_village, flags, nullptr, "village"));
}

TEST_CASE("zone scope: eval_condition shim threads zone_id through") {
  FlagStore flags;
  flags["zone.cave.chest"] = 1;

  CHECK(*corundum::dialogue::eval_condition("local.chest == 1", flags, nullptr, "cave"));
  CHECK_FALSE(*corundum::dialogue::eval_condition("local.chest == 1", flags, nullptr, "village"));
}

TEST_CASE("zone scope: visible_choices evaluates local.<key> conditions per zone") {
  using namespace corundum::dialogue;

  const auto looted = compile("local.chest_looted == 1");
  REQUIRE(looted.has_value());

  Node n;
  n.id = "gate";
  n.type = NodeType::Choice;
  n.choices = {
      {.label = "Always.", .target_id = "a"},
      {.label = "Looted the chest.", .target_id = "b", .condition = *looted},
  };

  FlagStore flags;
  flags["zone.cave.chest_looted"] = 1;

  const auto in_cave = visible_choices(n, flags, "any_graph", nullptr, "cave");
  REQUIRE(in_cave.size() == 2);

  const auto in_village = visible_choices(n, flags, "any_graph", nullptr, "village");
  REQUIRE(in_village.size() == 1);
  CHECK(in_village[0] == 0);
}

// ── reset_zone ───────────────────────────────────────────────────────────────

TEST_CASE("zone scope: reset_zone erases only the matching prefix") {
  FlagStore flags;
  flags["zone.cave.a"] = 1;
  flags["zone.cave.b"] = 2;
  flags["zone.village.a"] = 3;
  flags["quest.x"] = 1;
  flags["gold"] = 5;

  corundum::world::reset_zone(flags, "cave");

  CHECK_FALSE(flags.contains("zone.cave.a"));
  CHECK_FALSE(flags.contains("zone.cave.b"));
  // Other zones and globals survive.
  CHECK(flags["zone.village.a"] == 3);
  CHECK(flags["quest.x"] == 1);
  CHECK(flags["gold"] == 5);
}

TEST_CASE("zone scope: reset_zone does not clear zone ids that merely share a prefix") {
  FlagStore flags;
  flags["zone.cave.chest"] = 1;
  flags["zone.cave2.chest"] = 2;

  corundum::world::reset_zone(flags, "cave");

  CHECK_FALSE(flags.contains("zone.cave.chest"));
  CHECK(flags["zone.cave2.chest"] == 2);
}