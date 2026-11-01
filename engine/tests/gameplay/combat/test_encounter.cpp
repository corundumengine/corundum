// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/gameplay/combat/encounter.hpp>
#include <corundum/gameplay/rules/rules.hpp>
#include <corundum/world/picking.hpp>

#include <array>
#include <cstddef>
#include <expected>
#include <flat_map>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace {

  namespace combat = corundum::gameplay::combat;
  namespace rules = corundum::gameplay::rules;
  using corundum::world::TileCoord;

  using Specs = std::flat_map<std::string, rules::CombatantSpec, std::less<>>;

  constexpr std::string_view k_health = "health";

  /// The test rules use no dice and no flags.
  const rules::CombatContext k_context{};

  /// Two combatants one step apart, and two out of each other's reach.
  constexpr std::array<TileCoord, 2> k_adjacent{{{.col = 0, .row = 0}, {.col = 1, .row = 0}}};
  constexpr std::array<TileCoord, 2> k_apart{{{.col = 0, .row = 0}, {.col = 3, .row = 0}}};

  /// A fight between creatures named by id, with the numbers in @p specs. Each starts with
  /// @p health Health; every attack lands for 1 and reports "<attacker> hits <target>".
  rules::CombatRules make_rules(Specs specs, int health) {
    return rules::CombatRules{
        .command_point_beats = 100,
        .seconds_per_tick = 0.5f,
        .tempo_beats = 20,
        .attack =
            [](const rules::CombatContext &, const rules::Combatant &attacker, rules::Combatant &target) {
              target.state[k_health] -= 1;
              return std::format("{} hits {}", *attacker.creature, *target.creature);
            },
        .can_act = [](const rules::CombatContext &,
                      const rules::Combatant &combatant) { return combatant.state.find(k_health)->second > 0; },
        .health =
            [health](const rules::CombatContext &, const rules::Combatant &combatant) {
              return rules::Health{.current = combatant.state.find(k_health)->second, .max = health};
            },
        .join = [specs = std::move(specs), health](const rules::CombatContext &, rules::Combatant &combatant)
            -> std::expected<rules::CombatantSpec, std::string> {
          const auto found = specs.find(*combatant.creature);
          if (found == specs.end())
            return std::unexpected(std::format("no creature '{}'", *combatant.creature));
          combatant.state[k_health] = health;
          return found->second;
        },
    };
  }

  /// A combatant with reach 1 and a 20-beat wind-up returning to @p return_to.
  rules::CombatantSpec spec(int start_beats, int initiative = 0, int return_to = 0) {
    return {
        .attack = {.return_to_beats = return_to, .windup_beats = 20},
        .initiative = initiative,
        .name = "",
        .reach_tiles = 1,
        .start_beats = start_beats,
    };
  }

  combat::Joining party(std::string id) {
    return {.combatant = {.creature = std::move(id)}, .side = combat::Side::Party};
  }

  combat::Joining hostile(std::string id) {
    return {.combatant = {.creature = std::move(id)}, .side = combat::Side::Hostile};
  }

  /// One tick per step: "a" (index 0, the party) against "b" (index 1, a hostile).
  combat::Encounter start_duel(rules::CombatantSpec a, rules::CombatantSpec b, int health = 2) {
    std::expected<combat::Encounter, std::string> encounter = combat::Encounter::start(
        make_rules(Specs{{"a", a}, {"b", b}}, health), k_context, {party("a"), hostile("b")}, /*steps_per_tick=*/1);
    REQUIRE(encounter.has_value());
    return std::move(*encounter);
  }

} // namespace

TEST_CASE("Encounter: a marker starts at its start beats") {
  const combat::Encounter encounter = start_duel(spec(33), spec(41));

  CHECK_EQ(encounter.beats(0), 33);
  CHECK_EQ(encounter.beats(1), 41);
}

TEST_CASE("Encounter: a marker with no order holds at the command point") {
  combat::Encounter encounter = start_duel(spec(90), spec(0));

  std::ignore = encounter.update(k_context, k_adjacent);

  CHECK_EQ(encounter.beats(0), 100);
  CHECK_EQ(encounter.phase(0), combat::Phase::Ready);
}

TEST_CASE("Encounter: an order in reach winds up at the command point and lands a tick later") {
  combat::Encounter encounter = start_duel(spec(80), spec(0));
  encounter.order_attack(0, 1);
  const std::vector<std::string> first = encounter.update(k_context, k_adjacent);
  REQUIRE(first.empty());
  REQUIRE_EQ(encounter.phase(0), combat::Phase::WindUp);

  const std::vector<std::string> second = encounter.update(k_context, k_adjacent);

  CHECK_EQ(second, std::vector<std::string>{"a hits b"});
  CHECK_EQ(encounter.phase(0), combat::Phase::Ready);
  CHECK_EQ(encounter.beats(0), 0);
  CHECK_EQ(encounter.health(k_context, 1), rules::Health{.current = 1, .max = 2});
}

TEST_CASE("Encounter: an order out of reach holds at the command point until the target is in reach") {
  combat::Encounter encounter = start_duel(spec(80), spec(0));
  encounter.order_attack(0, 1);
  std::ignore = encounter.update(k_context, k_apart);
  std::ignore = encounter.update(k_context, k_apart);
  REQUIRE_EQ(encounter.phase(0), combat::Phase::Ready);
  REQUIRE_EQ(encounter.beats(0), 100);

  std::ignore = encounter.update(k_context, k_adjacent);

  CHECK_EQ(encounter.phase(0), combat::Phase::WindUp);
  CHECK_EQ(encounter.beats(0), 100);
}

TEST_CASE("Encounter: a tick comes every steps_per_tick-th step") {
  std::expected<combat::Encounter, std::string> encounter =
      combat::Encounter::start(make_rules(Specs{{"a", spec(0)}, {"b", spec(0)}}, 2), k_context,
                               {party("a"), hostile("b")}, /*steps_per_tick=*/3);
  REQUIRE(encounter.has_value());
  std::ignore = encounter->update(k_context, k_apart);
  std::ignore = encounter->update(k_context, k_apart);
  REQUIRE_EQ(encounter->beats(0), 0);

  std::ignore = encounter->update(k_context, k_apart);

  CHECK_EQ(encounter->beats(0), 20);
}

TEST_CASE("Encounter: a target that leaves reach during the wind-up makes the attack fizzle") {
  combat::Encounter encounter = start_duel(spec(80, 0, /*return_to=*/40), spec(0));
  encounter.order_attack(0, 1);
  std::ignore = encounter.update(k_context, k_adjacent);

  const std::vector<std::string> reports = encounter.update(k_context, k_apart);

  CHECK(reports.empty());
  CHECK_EQ(encounter.beats(0), 40);
  CHECK_EQ(encounter.phase(0), combat::Phase::Ready);
  CHECK_EQ(encounter.target_of(0), std::optional<std::size_t>{1});
  CHECK_EQ(encounter.health(k_context, 1), rules::Health{.current = 2, .max = 2});
}

TEST_CASE("Encounter: order_move cancels a wind-up to the return point and drops the order") {
  combat::Encounter encounter = start_duel(spec(80, 0, /*return_to=*/40), spec(0));
  encounter.order_attack(0, 1);
  std::ignore = encounter.update(k_context, k_adjacent);
  REQUIRE_EQ(encounter.phase(0), combat::Phase::WindUp);

  encounter.order_move(0);

  CHECK_EQ(encounter.phase(0), combat::Phase::Ready);
  CHECK_EQ(encounter.beats(0), 40);
  CHECK_FALSE(encounter.target_of(0).has_value());
}

TEST_CASE("Encounter: of two attacks landing on one tick, the one further along goes first") {
  // b is further along (133 against 125), so it lands first although a is the party.
  combat::Encounter encounter = start_duel(spec(85), spec(93));
  encounter.order_attack(0, 1);
  encounter.order_attack(1, 0);
  std::ignore = encounter.update(k_context, k_adjacent);

  const std::vector<std::string> reports = encounter.update(k_context, k_adjacent);

  CHECK_EQ(reports, std::vector<std::string>{"b hits a", "a hits b"});
}

TEST_CASE("Encounter: of two attacks equally far along, higher initiative goes first") {
  combat::Encounter encounter = start_duel(spec(80, /*initiative=*/3), spec(80, /*initiative=*/5));
  encounter.order_attack(0, 1);
  encounter.order_attack(1, 0);
  std::ignore = encounter.update(k_context, k_adjacent);

  const std::vector<std::string> reports = encounter.update(k_context, k_adjacent);

  CHECK_EQ(reports, std::vector<std::string>{"b hits a", "a hits b"});
}

TEST_CASE("Encounter: of two attacks equally far along with equal initiative, the party goes first") {
  // The hostile joins first, so join order alone would put it first.
  std::expected<combat::Encounter, std::string> encounter =
      combat::Encounter::start(make_rules(Specs{{"a", spec(80, 4)}, {"b", spec(80, 4)}}, 2), k_context,
                               {hostile("b"), party("a")}, /*steps_per_tick=*/1);
  REQUIRE(encounter.has_value());
  encounter->order_attack(0, 1);
  encounter->order_attack(1, 0);
  std::ignore = encounter->update(k_context, k_adjacent);

  const std::vector<std::string> reports = encounter->update(k_context, k_adjacent);

  CHECK_EQ(reports, std::vector<std::string>{"a hits b", "b hits a"});
}

TEST_CASE("Encounter: a combatant downed earlier in the tick doesn't land its attack") {
  combat::Encounter encounter = start_duel(spec(93), spec(85), /*health=*/1);
  encounter.order_attack(0, 1);
  encounter.order_attack(1, 0);
  std::ignore = encounter.update(k_context, k_adjacent);

  const std::vector<std::string> reports = encounter.update(k_context, k_adjacent);

  CHECK_EQ(reports, std::vector<std::string>{"a hits b"});
}

TEST_CASE("Encounter: the party wins once no hostile can act") {
  combat::Encounter encounter = start_duel(spec(80), spec(0), /*health=*/1);
  encounter.order_attack(0, 1);
  std::ignore = encounter.update(k_context, k_adjacent);
  REQUIRE_FALSE(encounter.outcome().has_value());

  std::ignore = encounter.update(k_context, k_adjacent);

  CHECK_EQ(encounter.outcome(), std::optional<combat::Outcome>{combat::Outcome::Won});
}

TEST_CASE("Encounter: the party loses once no party member can act") {
  combat::Encounter encounter = start_duel(spec(0), spec(80), /*health=*/1);
  encounter.order_attack(1, 0);
  std::ignore = encounter.update(k_context, k_adjacent);

  std::ignore = encounter.update(k_context, k_adjacent);

  CHECK_EQ(encounter.outcome(), std::optional<combat::Outcome>{combat::Outcome::Lost});
}

TEST_CASE("Encounter: an order on a downed target is dropped") {
  combat::Encounter encounter = start_duel(spec(80), spec(0), /*health=*/1);
  encounter.order_attack(0, 1);
  std::ignore = encounter.update(k_context, k_adjacent);

  std::ignore = encounter.update(k_context, k_adjacent);

  CHECK_FALSE(encounter.target_of(0).has_value());
}

TEST_CASE("Encounter: nothing advances once the fight is over") {
  combat::Encounter encounter = start_duel(spec(80), spec(0), /*health=*/1);
  encounter.order_attack(0, 1);
  std::ignore = encounter.update(k_context, k_adjacent);
  std::ignore = encounter.update(k_context, k_adjacent);
  REQUIRE(encounter.outcome().has_value());

  std::ignore = encounter.update(k_context, k_adjacent);

  CHECK_EQ(encounter.beats(1), 40);
}

TEST_CASE("Encounter::start names the combatant whose join failed") {
  const std::expected<combat::Encounter, std::string> encounter = combat::Encounter::start(
      make_rules(Specs{{"a", spec(0)}}, 2), k_context, {party("a"), hostile("ghost")}, /*steps_per_tick=*/1);

  REQUIRE_FALSE(encounter.has_value());
  CHECK_EQ(encounter.error(), "[gameplay] 'ghost' can't join the encounter: no creature 'ghost'");
}

TEST_CASE("Encounter::start refuses rules with an empty callback") {
  rules::CombatRules incomplete = make_rules(Specs{{"a", spec(0)}, {"b", spec(0)}}, 2);
  incomplete.attack = {};

  const std::expected<combat::Encounter, std::string> encounter =
      combat::Encounter::start(std::move(incomplete), k_context, {party("a"), hostile("b")}, /*steps_per_tick=*/1);

  REQUIRE_FALSE(encounter.has_value());
  CHECK_EQ(encounter.error(), "[gameplay] the combat rules have no 'attack' callback");
}

TEST_CASE("Encounter::start refuses a timeline that never advances") {
  rules::CombatRules stalled = make_rules(Specs{{"a", spec(0)}, {"b", spec(0)}}, 2);
  stalled.tempo_beats = 0;

  const std::expected<combat::Encounter, std::string> encounter =
      combat::Encounter::start(std::move(stalled), k_context, {party("a"), hostile("b")}, /*steps_per_tick=*/1);

  REQUIRE_FALSE(encounter.has_value());
  CHECK_EQ(encounter.error(), "[gameplay] the combat rules need a positive command_point_beats and tempo_beats");
}
