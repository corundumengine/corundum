// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/gameplay/combat/encounter.hpp>
#include <corundum/gameplay/rules/rules.hpp>
#include <corundum/world/picking.hpp>

#include <algorithm>
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

  /// A hostile's id and the id of the command it always chooses.
  using Plans = std::flat_map<std::string, std::string, std::less<>>;

  constexpr std::string_view k_braced = "braced";

  /// Indices into test_commands().
  constexpr std::size_t k_jab = 0;
  constexpr std::size_t k_brace = 1;
  constexpr std::size_t k_bash = 2;
  constexpr std::size_t k_trip = 3;
  constexpr std::size_t k_nudge = 4;

  /// Every test combatant's commands, in this order: jab (Quick, at an enemy), brace (Quick, on
  /// itself, available until used), bash (Action, Breaks), trip (Action, Staggers 50 beats), nudge
  /// (Action, Staggers 10 beats) and smash (Heavy, at an enemy).
  std::vector<rules::CommandOption> test_commands(const rules::Combatant &combatant) {
    const auto at_enemy = [](std::string id, std::string label, int return_to, int windup) {
      return rules::CommandOption{
          .id = std::move(id),
          .is_available = true,
          .label = std::move(label),
          .target = rules::CommandTarget::Enemy,
          .timing = {.return_to_beats = return_to, .windup_beats = windup},
      };
    };
    return {
        at_enemy("jab", "Jab", 40, 10),
        {
            .id = "brace",
            .is_available = !combatant.state.contains(k_braced),
            .label = "Brace",
            .target = rules::CommandTarget::Self,
            .timing = {.return_to_beats = 40, .windup_beats = 10},
        },
        at_enemy("bash", "Bash", 0, 20),
        at_enemy("trip", "Trip", 0, 20),
        at_enemy("nudge", "Nudge", 0, 20),
        at_enemy("smash", "Smash", 0, 30),
    };
  }

  /// How far each test command Staggers: trip 50 beats, nudge 10, any other none.
  int stagger_of(std::string_view command) {
    if (command == "trip")
      return 50;
    if (command == "nudge")
      return 10;
    return 0;
  }

  /// A fight between creatures named by id, with the numbers in @p specs and each named after its
  /// id. Each starts with @p health Health. Every hit lands for 1 and reports "<attacker> hits
  /// <target>", or "<attacker> uses <command> on <target>"; bash Breaks, and trip and nudge Stagger.
  /// A hostile in @p plans always chooses its planned command. A Broken marker drops to 10.
  rules::CombatRules make_rules(Specs specs, int health, Plans plans = {}) {
    return rules::CombatRules{
        .auto_pause = std::nullopt,
        .break_return_beats = 10,
        .command_point_beats = 100,
        .seconds_per_tick = 0.5f,
        .tempo_beats = 20,
        .can_act = [](const rules::CombatContext &,
                      const rules::Combatant &combatant) { return combatant.state.find(k_health)->second > 0; },
        .commands = [](const rules::CombatContext &,
                       const rules::Combatant &combatant) { return test_commands(combatant); },
        .health =
            [health](const rules::CombatContext &, const rules::Combatant &combatant) {
              return rules::Health{.current = combatant.state.find(k_health)->second, .max = health};
            },
        .hostile_command = [plans = std::move(plans)](const rules::CombatContext &, rules::Combatant &hostile)
            -> std::optional<rules::CommandOption> {
          const auto planned = plans.find(*hostile.creature);
          if (planned == plans.end())
            return std::nullopt;
          const std::vector<rules::CommandOption> options = test_commands(hostile);
          const auto chosen = std::ranges::find(options, planned->second, &rules::CommandOption::id);
          REQUIRE(chosen != options.end());
          return *chosen;
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
            [](const rules::CombatContext &, std::string_view command, rules::Combatant &actor,
               rules::Combatant &target) {
              target.state[k_health] -= 1;
              if (command.empty())
                return rules::CommandResult{.report = std::format("{} hits {}", *actor.creature, *target.creature)};
              return rules::CommandResult{
                  .breaks = command == "bash",
                  .report = std::format("{} uses {} on {}", *actor.creature, command, *target.creature),
                  .stagger_beats = stagger_of(command),
              };
            },
        .resolve_self =
            [](const rules::CombatContext &, std::string_view command, rules::Combatant &actor) {
              actor.state[k_braced] = 1;
              return std::format("{} uses {}", *actor.creature, command);
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
  combat::Encounter start_duel(rules::CombatantSpec a, rules::CombatantSpec b, int health = 2, Plans plans = {}) {
    std::expected<combat::Encounter, std::string> encounter =
        combat::Encounter::start(make_rules(Specs{{"a", a}, {"b", b}}, health, std::move(plans)), k_context,
                                 {party("a"), hostile("b")}, /*steps_per_tick=*/1);
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

TEST_CASE("Encounter: a target that leaves reach during the wind-up makes the attack fizzle") {
  combat::Encounter encounter = start_duel(spec(80, 0, /*return_to=*/40), spec(0));
  encounter.order_attack(0, 1);
  std::ignore = encounter.update(k_context, k_adjacent);

  const std::vector<std::string> reports = encounter.update(k_context, k_apart).reports;

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

  const std::vector<std::string> reports = encounter.update(k_context, k_adjacent).reports;

  CHECK_EQ(reports, std::vector<std::string>{"b hits a", "a hits b"});
}

TEST_CASE("Encounter: of two attacks equally far along, higher initiative goes first") {
  combat::Encounter encounter = start_duel(spec(80, /*initiative=*/3), spec(80, /*initiative=*/5));
  encounter.order_attack(0, 1);
  encounter.order_attack(1, 0);
  std::ignore = encounter.update(k_context, k_adjacent);

  const std::vector<std::string> reports = encounter.update(k_context, k_adjacent).reports;

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
  CHECK_EQ(encounter.error(), "[gameplay] the combat rules need a positive command_point_beats and tempo_beats");
}

TEST_CASE("Encounter: a queued command winds up at the command point instead of the standing attack") {
  combat::Encounter encounter = start_duel(spec(80), spec(0));
  encounter.order_attack(0, 1);
  encounter.queue_command(0, k_jab);

  std::ignore = encounter.update(k_context, k_adjacent);

  CHECK_EQ(encounter.phase(0), combat::Phase::WindUp);
  CHECK_EQ(encounter.windup_label(0), "Jab");
  CHECK_EQ(encounter.windup_end_beats(0), 110);
  CHECK_FALSE(encounter.queued(0).has_value());
}

TEST_CASE("Encounter: a queued command lands, drops to its own return point, and the standing attack comes next") {
  combat::Encounter encounter = start_duel(spec(80), spec(0));
  encounter.order_attack(0, 1);
  encounter.queue_command(0, k_jab);
  std::ignore = encounter.update(k_context, k_adjacent);

  const combat::EncounterStep landed = encounter.update(k_context, k_adjacent);

  CHECK_EQ(landed.reports, std::vector<std::string>{"a uses jab on b"});
  CHECK_EQ(encounter.beats(0), 40);
  CHECK_EQ(encounter.windup_end_beats(0), 120);
}

TEST_CASE("Encounter: a Self command winds up with no target and resolves on its actor") {
  combat::Encounter encounter = start_duel(spec(80), spec(0));
  encounter.queue_command(0, k_brace);
  std::ignore = encounter.update(k_context, k_adjacent);
  REQUIRE_EQ(encounter.windup_label(0), "Brace");

  const combat::EncounterStep landed = encounter.update(k_context, k_adjacent);

  CHECK_EQ(landed.reports, std::vector<std::string>{"a uses brace"});
  CHECK_EQ(encounter.beats(0), 40);
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

  CHECK_EQ(labels, std::vector<std::string>{"Jab", "Brace", "Bash", "Trip", "Nudge", "Smash"});
}

TEST_CASE("Encounter: an Enemy command waits at the command point until its target is in reach") {
  combat::Encounter encounter = start_duel(spec(80), spec(0));
  encounter.order_attack(0, 1);
  encounter.queue_command(0, k_jab);
  std::ignore = encounter.update(k_context, k_apart);
  std::ignore = encounter.update(k_context, k_apart);
  REQUIRE_EQ(encounter.phase(0), combat::Phase::Ready);
  REQUIRE_EQ(encounter.beats(0), 100);

  std::ignore = encounter.update(k_context, k_adjacent);

  CHECK_EQ(encounter.phase(0), combat::Phase::WindUp);
  CHECK_EQ(encounter.windup_label(0), "Jab");
}

TEST_CASE("Encounter: a hostile winds up the command its rules choose") {
  combat::Encounter encounter = start_duel(spec(0), spec(80), 2, Plans{{"b", "smash"}});
  encounter.order_attack(1, 0);

  std::ignore = encounter.update(k_context, k_adjacent);

  CHECK_EQ(encounter.windup_label(1), "Smash");
  CHECK_EQ(encounter.windup_end_beats(1), 130);
}

TEST_CASE("Encounter: a hostile's heavy command lands when its longer wind-up ends") {
  combat::Encounter encounter = start_duel(spec(0), spec(80), 2, Plans{{"b", "smash"}});
  encounter.order_attack(1, 0);
  std::ignore = encounter.update(k_context, k_adjacent);
  const combat::EncounterStep early = encounter.update(k_context, k_adjacent);
  REQUIRE(early.reports.empty());

  const combat::EncounterStep landed = encounter.update(k_context, k_adjacent);

  CHECK_EQ(landed.reports, std::vector<std::string>{"b uses smash on a"});
}

TEST_CASE("Encounter: update names the combatants whose wind-up started") {
  combat::Encounter encounter = start_duel(spec(80), spec(80));
  encounter.order_attack(0, 1);
  encounter.order_attack(1, 0);

  const combat::EncounterStep step = encounter.update(k_context, k_adjacent);

  CHECK_EQ(step.windups_started, std::vector<std::size_t>{0, 1});
}

TEST_CASE("Encounter: a Break cancels the target's wind-up and drops its marker to the break return point") {
  combat::Encounter encounter = start_duel(spec(80), spec(80), 2, Plans{{"b", "smash"}});
  encounter.order_attack(0, 1);
  encounter.order_attack(1, 0);
  encounter.queue_command(0, k_bash);
  std::ignore = encounter.update(k_context, k_adjacent);

  const combat::EncounterStep step = encounter.update(k_context, k_adjacent);

  CHECK_EQ(step.reports, std::vector<std::string>{"a uses bash on b", "b: Smash broken"});
  CHECK_EQ(encounter.phase(1), combat::Phase::Ready);
  CHECK_EQ(encounter.beats(1), 10);
  CHECK(encounter.windup_label(1).empty());
}

TEST_CASE("Encounter: a Break on a combatant that isn't winding up leaves its marker alone") {
  combat::Encounter encounter = start_duel(spec(80), spec(0));
  encounter.order_attack(0, 1);
  encounter.queue_command(0, k_bash);
  std::ignore = encounter.update(k_context, k_adjacent);

  const combat::EncounterStep step = encounter.update(k_context, k_adjacent);

  CHECK_EQ(step.reports, std::vector<std::string>{"a uses bash on b"});
  CHECK_EQ(encounter.beats(1), 40);
}

TEST_CASE("Encounter: a Stagger knocks a wind-up back without cancelling it") {
  combat::Encounter encounter = start_duel(spec(80), spec(80), 2, Plans{{"b", "smash"}});
  encounter.order_attack(0, 1);
  encounter.order_attack(1, 0);
  encounter.queue_command(0, k_trip);
  std::ignore = encounter.update(k_context, k_adjacent);

  std::ignore = encounter.update(k_context, k_adjacent);

  CHECK_EQ(encounter.phase(1), combat::Phase::WindUp);
  CHECK_EQ(encounter.beats(1), 70);
  CHECK_EQ(encounter.windup_label(1), "Smash");
}

TEST_CASE("Encounter: a Stagger never takes a marker below 0") {
  combat::Encounter encounter = start_duel(spec(80), spec(0));
  encounter.order_attack(0, 1);
  encounter.queue_command(0, k_trip);
  std::ignore = encounter.update(k_context, k_adjacent);

  std::ignore = encounter.update(k_context, k_adjacent);

  CHECK_EQ(encounter.beats(1), 0);
}

TEST_CASE("Encounter: a combatant Broken earlier in the tick doesn't land its own command") {
  // a is further along (130 against 120), so its bash lands first and Breaks b's attack.
  combat::Encounter encounter = start_duel(spec(90), spec(80));
  encounter.order_attack(0, 1);
  encounter.order_attack(1, 0);
  encounter.queue_command(0, k_bash);
  std::ignore = encounter.update(k_context, k_adjacent);

  const combat::EncounterStep step = encounter.update(k_context, k_adjacent);

  CHECK_EQ(step.reports, std::vector<std::string>{"a uses bash on b", "b: Attack broken"});
  CHECK_EQ(encounter.health(k_context, 0), rules::Health{.current = 2, .max = 2});
}

TEST_CASE("Encounter: a combatant Staggered below its landing point earlier in the tick doesn't land") {
  combat::Encounter encounter = start_duel(spec(90), spec(80));
  encounter.order_attack(0, 1);
  encounter.order_attack(1, 0);
  encounter.queue_command(0, k_trip);
  std::ignore = encounter.update(k_context, k_adjacent);

  const combat::EncounterStep step = encounter.update(k_context, k_adjacent);

  CHECK_EQ(step.reports, std::vector<std::string>{"a uses trip on b"});
  CHECK_EQ(encounter.phase(1), combat::Phase::WindUp);
  CHECK_EQ(encounter.beats(1), 70);
}

TEST_CASE("Encounter: a Stagger smaller than the target's overshoot leaves its command landing") {
  // Both land on tick 2: a at 139, b at 135, both past their 120 landing point. a goes first, and
  // its nudge takes b to 125, still past 120.
  combat::Encounter encounter = start_duel(spec(99), spec(95));
  encounter.order_attack(0, 1);
  encounter.order_attack(1, 0);
  encounter.queue_command(0, k_nudge);
  std::ignore = encounter.update(k_context, k_adjacent);

  const combat::EncounterStep step = encounter.update(k_context, k_adjacent);

  CHECK_EQ(step.reports, std::vector<std::string>{"a uses nudge on b", "b hits a"});
  CHECK_EQ(encounter.phase(1), combat::Phase::Ready);
}

TEST_CASE("Encounter: order_move cancels the command winding up and keeps a queued one") {
  combat::Encounter encounter = start_duel(spec(80), spec(0));
  encounter.order_attack(0, 1);
  encounter.queue_command(0, k_jab);
  std::ignore = encounter.update(k_context, k_adjacent);
  encounter.queue_command(0, k_brace);

  encounter.order_move(0);

  CHECK_EQ(encounter.phase(0), combat::Phase::Ready);
  CHECK_EQ(encounter.beats(0), 40);
  CHECK_EQ(encounter.queued(0), std::optional<std::size_t>{k_brace});
  CHECK_FALSE(encounter.target_of(0).has_value());
}

TEST_CASE("Encounter: a queued command that fizzles is spent, and the standing attack stays") {
  combat::Encounter encounter = start_duel(spec(80), spec(0));
  encounter.order_attack(0, 1);
  encounter.queue_command(0, k_jab);
  std::ignore = encounter.update(k_context, k_adjacent);

  const combat::EncounterStep step = encounter.update(k_context, k_apart);

  CHECK(step.reports.empty());
  CHECK_EQ(encounter.beats(0), 40);
  CHECK_EQ(encounter.target_of(0), std::optional<std::size_t>{1});
  CHECK_EQ(encounter.windup_end_beats(0), 120);
}
