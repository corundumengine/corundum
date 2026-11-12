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

  constexpr std::string_view k_braced = "braced";

  /// Indices into test_commands().
  constexpr std::size_t k_brace = 0;
  constexpr std::size_t k_focus = 1;

  /// Every test combatant's commands, in this order: brace (available until used) and focus (always
  /// available).
  std::vector<rules::CommandOption> test_commands(const rules::Combatant &combatant) {
    return {
        {.id = "brace", .is_available = !combatant.state.contains(k_braced), .label = "Brace"},
        {.id = "focus", .is_available = true, .label = "Focus"},
    };
  }

  /// A fight between creatures named by id, with the numbers in @p specs and each named after its
  /// id. Each starts with @p health Health. Reach is 1 tile and every wind-up 20 beats. An attack
  /// lands for 1 and reports "<attacker> hits <target>"; a command reports "<actor> uses
  /// <command>", and brace marks its actor braced.
  rules::CombatRules make_rules(Specs specs, int health) {
    return rules::CombatRules{
        .command_point_beats = 100,
        .reach_tiles = 1,
        .seconds_per_tick = 0.5f,
        .tempo_beats = 20,
        .windup_beats = 20,
        .can_act = [](const rules::CombatContext &,
                      const rules::Combatant &combatant) { return combatant.state.find(k_health)->second > 0; },
        .commands = [](const rules::CombatContext &,
                       const rules::Combatant &combatant) { return test_commands(combatant); },
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
          rules::CombatantSpec joined = found->second;
          joined.name = *combatant.creature;
          return joined;
        },
        .resolve =
            [](const rules::CombatContext &, rules::Combatant &actor, rules::Combatant &target) {
              target.state[k_health] -= 1;
              return std::format("{} hits {}", *actor.creature, *target.creature);
            },
        .resolve_self =
            [](const rules::CombatContext &, std::string_view command, rules::Combatant &actor) {
              if (command == "brace")
                actor.state[k_braced] = 1;
              return std::format("{} uses {}", *actor.creature, command);
            },
    };
  }

  /// A combatant whose marker starts at @p start_beats.
  rules::CombatantSpec spec(int start_beats) {
    return {.name = "", .start_beats = start_beats};
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

  /// One tick per step: "a" (index 0, the party) against "b" (index 1, a hostile) under @p combat_rules.
  combat::Encounter start_with(rules::CombatRules combat_rules) {
    std::expected<combat::Encounter, std::string> encounter =
        combat::Encounter::start(std::move(combat_rules), k_context, {party("a"), hostile("b")}, /*steps_per_tick=*/1);
    REQUIRE(encounter.has_value());
    return std::move(*encounter);
  }

  /// The default test rules for "a" and "b", with every Health at 2.
  rules::CombatRules duel_rules(rules::CombatantSpec a, rules::CombatantSpec b) {
    return make_rules(Specs{{"a", a}, {"b", b}}, 2);
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
  const std::vector<std::string> first = encounter.update(k_context, k_adjacent).reports;
  REQUIRE(first.empty());
  REQUIRE_EQ(encounter.phase(0), combat::Phase::WindUp);

  const std::vector<std::string> second = encounter.update(k_context, k_adjacent).reports;

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

TEST_CASE("Encounter: of two attacks landing on one tick, the one further along goes first") {
  // b is further along (133 against 125), so it lands first although a is the party.
  combat::Encounter encounter = start_duel(spec(85), spec(93));
  encounter.order_attack(0, 1);
  encounter.order_attack(1, 0);
  std::ignore = encounter.update(k_context, k_adjacent);

  const std::vector<std::string> reports = encounter.update(k_context, k_adjacent).reports;

  CHECK_EQ(reports, std::vector<std::string>{"b hits a", "a hits b"});
}

TEST_CASE("Encounter: of two attacks equally far along, the party goes first") {
  // The hostile joins first, so join order alone would put it first.
  std::expected<combat::Encounter, std::string> encounter =
      combat::Encounter::start(make_rules(Specs{{"a", spec(80)}, {"b", spec(80)}}, 2), k_context,
                               {hostile("b"), party("a")}, /*steps_per_tick=*/1);
  REQUIRE(encounter.has_value());
  encounter->order_attack(0, 1);
  encounter->order_attack(1, 0);
  std::ignore = encounter->update(k_context, k_adjacent);

  const std::vector<std::string> reports = encounter->update(k_context, k_adjacent).reports;

  CHECK_EQ(reports, std::vector<std::string>{"a hits b", "b hits a"});
}

TEST_CASE("Encounter: a combatant downed earlier in the tick doesn't land its attack") {
  combat::Encounter encounter = start_duel(spec(93), spec(85), /*health=*/1);
  encounter.order_attack(0, 1);
  encounter.order_attack(1, 0);
  std::ignore = encounter.update(k_context, k_adjacent);

  const std::vector<std::string> reports = encounter.update(k_context, k_adjacent).reports;

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
  incomplete.resolve = {};

  const std::expected<combat::Encounter, std::string> encounter =
      combat::Encounter::start(std::move(incomplete), k_context, {party("a"), hostile("b")}, /*steps_per_tick=*/1);

  REQUIRE_FALSE(encounter.has_value());
  CHECK_EQ(encounter.error(), "[gameplay] the combat rules have no 'resolve' callback");
}

TEST_CASE("Encounter::start refuses a timeline that never advances") {
  rules::CombatRules stalled = make_rules(Specs{{"a", spec(0)}, {"b", spec(0)}}, 2);
  stalled.tempo_beats = 0;

  const std::expected<combat::Encounter, std::string> encounter =
      combat::Encounter::start(std::move(stalled), k_context, {party("a"), hostile("b")}, /*steps_per_tick=*/1);

  REQUIRE_FALSE(encounter.has_value());
  CHECK_EQ(encounter.error(),
           "[gameplay] the combat rules need a positive command_point_beats, tempo_beats and reach_tiles");
}

TEST_CASE("Encounter::start refuses rules whose reach is below one tile") {
  rules::CombatRules reachless = duel_rules(spec(0), spec(0));
  reachless.reach_tiles = 0;

  const std::expected<combat::Encounter, std::string> encounter =
      combat::Encounter::start(std::move(reachless), k_context, {party("a"), hostile("b")}, /*steps_per_tick=*/1);

  REQUIRE_FALSE(encounter.has_value());
  CHECK_EQ(encounter.error(),
           "[gameplay] the combat rules need a positive command_point_beats, tempo_beats and reach_tiles");
}

TEST_CASE("Encounter: an attack reaches as far as the rules' reach") {
  rules::CombatRules long_reach = duel_rules(spec(80), spec(0));
  long_reach.reach_tiles = 3;
  combat::Encounter encounter = start_with(std::move(long_reach));
  encounter.order_attack(0, 1);

  std::ignore = encounter.update(k_context, k_apart); // k_apart is 3 tiles.

  CHECK_EQ(encounter.phase(0), combat::Phase::WindUp);
}

TEST_CASE("Encounter: a target that leaves reach during the wind-up makes the attack fizzle") {
  combat::Encounter encounter = start_duel(spec(80), spec(0));
  encounter.order_attack(0, 1);
  std::ignore = encounter.update(k_context, k_adjacent);

  const std::vector<std::string> reports = encounter.update(k_context, k_apart).reports;

  CHECK(reports.empty());
  CHECK_EQ(encounter.beats(0), 0);
  CHECK_EQ(encounter.phase(0), combat::Phase::Ready);
  CHECK_EQ(encounter.target_of(0), std::optional<std::size_t>{1});
  CHECK_EQ(encounter.health(k_context, 1), rules::Health{.current = 2, .max = 2});
}

TEST_CASE("Encounter: order_move cancels a wind-up to 0 and drops the order") {
  combat::Encounter encounter = start_duel(spec(80), spec(0));
  encounter.order_attack(0, 1);
  std::ignore = encounter.update(k_context, k_adjacent);
  REQUIRE_EQ(encounter.phase(0), combat::Phase::WindUp);

  encounter.order_move(0);

  CHECK_EQ(encounter.phase(0), combat::Phase::Ready);
  CHECK_EQ(encounter.beats(0), 0);
  CHECK_FALSE(encounter.target_of(0).has_value());
}

TEST_CASE("Encounter: a queued command winds up at the command point instead of the standing attack") {
  combat::Encounter encounter = start_duel(spec(80), spec(0));
  encounter.order_attack(0, 1);
  encounter.queue_command(0, k_focus);

  std::ignore = encounter.update(k_context, k_adjacent);

  CHECK_EQ(encounter.phase(0), combat::Phase::WindUp);
  CHECK_FALSE(encounter.is_winding_up_at_enemy(0));
  CHECK_FALSE(encounter.queued(0).has_value());
}

TEST_CASE("Encounter: a queued command lands, drops to 0, and the standing attack comes next") {
  combat::Encounter encounter = start_duel(spec(80), spec(0));
  encounter.order_attack(0, 1);
  encounter.queue_command(0, k_focus);
  std::ignore = encounter.update(k_context, k_adjacent);
  const combat::EncounterStep landed = encounter.update(k_context, k_adjacent);
  REQUIRE_EQ(landed.reports, std::vector<std::string>{"a uses focus"});
  REQUIRE_EQ(encounter.beats(0), 0);

  // From 0 to the command point at 20 a tick is 5 ticks.
  for (int tick = 0; tick < 5; ++tick)
    std::ignore = encounter.update(k_context, k_adjacent);

  CHECK(encounter.is_winding_up_at_enemy(0));
}

TEST_CASE("Encounter: a queued command resolves on its actor with no target and drops to 0") {
  combat::Encounter encounter = start_duel(spec(80), spec(0));
  encounter.queue_command(0, k_brace);
  std::ignore = encounter.update(k_context, k_apart);

  const combat::EncounterStep landed = encounter.update(k_context, k_apart);

  CHECK_EQ(landed.reports, std::vector<std::string>{"a uses brace"});
  CHECK_EQ(encounter.beats(0), 0);
}

TEST_CASE("Encounter: a command's availability refreshes once a command resolves") {
  combat::Encounter encounter = start_duel(spec(80), spec(0));
  REQUIRE(encounter.commands(0)[k_brace].is_available);
  encounter.queue_command(0, k_brace);
  std::ignore = encounter.update(k_context, k_adjacent);

  std::ignore = encounter.update(k_context, k_adjacent);

  CHECK_FALSE(encounter.commands(0)[k_brace].is_available);
}

TEST_CASE("Encounter: commands lists the rules' options in their order") {
  const combat::Encounter encounter = start_duel(spec(0), spec(0));

  std::vector<std::string> labels;
  for (const rules::CommandOption &option : encounter.commands(0))
    labels.push_back(option.label);

  CHECK_EQ(labels, std::vector<std::string>{"Brace", "Focus"});
}

TEST_CASE("Encounter: a queued command starts at the command point while the target is out of reach") {
  combat::Encounter encounter = start_duel(spec(80), spec(0));
  encounter.order_attack(0, 1);
  encounter.queue_command(0, k_focus);

  std::ignore = encounter.update(k_context, k_apart);

  CHECK_EQ(encounter.phase(0), combat::Phase::WindUp);
  CHECK_EQ(encounter.beats(0), 100);
}

TEST_CASE("Encounter: update names the combatants whose wind-up started") {
  combat::Encounter encounter = start_duel(spec(80), spec(80));
  encounter.order_attack(0, 1);
  encounter.order_attack(1, 0);

  const combat::EncounterStep step = encounter.update(k_context, k_adjacent);

  CHECK_EQ(step.windups_started, std::vector<std::size_t>{0, 1});
}

TEST_CASE("Encounter: order_move cancels the command winding up and keeps a queued one") {
  combat::Encounter encounter = start_duel(spec(80), spec(0));
  encounter.order_attack(0, 1);
  encounter.queue_command(0, k_focus);
  std::ignore = encounter.update(k_context, k_adjacent);
  encounter.queue_command(0, k_brace);

  encounter.order_move(0);

  CHECK_EQ(encounter.phase(0), combat::Phase::Ready);
  CHECK_EQ(encounter.beats(0), 0);
  CHECK_EQ(encounter.queued(0), std::optional<std::size_t>{k_brace});
  CHECK_FALSE(encounter.target_of(0).has_value());
}

TEST_CASE("Encounter: windup_seconds counts whole ticks from the command point") {
  rules::CombatRules slow = duel_rules(spec(80), spec(0));
  slow.windup_beats = 30;
  combat::Encounter encounter = start_with(std::move(slow));
  encounter.order_attack(0, 1);

  std::ignore = encounter.update(k_context, k_adjacent);

  REQUIRE_EQ(encounter.phase(0), combat::Phase::WindUp);
  CHECK_EQ(encounter.windup_seconds(0), 1.f); // 30 beats at 20 a tick lands on the second tick.
}

TEST_CASE("Encounter: windup_seconds of the standing attack is one tick") {
  combat::Encounter encounter = start_duel(spec(80), spec(0));
  encounter.order_attack(0, 1);

  std::ignore = encounter.update(k_context, k_adjacent);

  CHECK_EQ(encounter.windup_seconds(0), 0.5f);
}

TEST_CASE("Encounter: windup_seconds counts only the beats left past the command point") {
  rules::CombatRules slow = duel_rules(spec(95), spec(0));
  slow.windup_beats = 30;
  combat::Encounter encounter = start_with(std::move(slow));
  encounter.order_attack(0, 1);

  std::ignore = encounter.update(k_context, k_adjacent);

  REQUIRE_EQ(encounter.beats(0), 115);
  CHECK_EQ(encounter.windup_seconds(0), 0.5f); // 15 of the 30 beats left: it lands on the next tick.
}

TEST_CASE("Encounter: the standing attack winds up at an enemy") {
  combat::Encounter encounter = start_duel(spec(80), spec(0));
  encounter.order_attack(0, 1);

  std::ignore = encounter.update(k_context, k_adjacent);

  CHECK(encounter.is_winding_up_at_enemy(0));
}

TEST_CASE("Encounter: a queued command doesn't wind up at an enemy") {
  combat::Encounter encounter = start_duel(spec(80), spec(0));
  encounter.queue_command(0, k_brace);

  std::ignore = encounter.update(k_context, k_adjacent);

  REQUIRE_EQ(encounter.phase(0), combat::Phase::WindUp);
  CHECK_FALSE(encounter.is_winding_up_at_enemy(0));
}

TEST_CASE("Encounter: a combatant not winding up isn't winding up at an enemy") {
  const combat::Encounter encounter = start_duel(spec(0), spec(0));

  CHECK_FALSE(encounter.is_winding_up_at_enemy(0));
}
