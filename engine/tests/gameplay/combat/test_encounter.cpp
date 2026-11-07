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
  constexpr std::size_t k_smash = 5;

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

  /// The no-op turn, tick and toggle callbacks the test rules use.
  rules::TurnEndResult no_turn_end(const rules::CombatContext & /*context*/, rules::Combatant & /*combatant*/,
                                   rules::TurnEnd /*how*/) {
    return {};
  }

  std::vector<std::string> no_turn_start(const rules::CombatContext & /*context*/, rules::Combatant & /*combatant*/) {
    return {};
  }

  rules::CombatantStatus no_status(const rules::CombatContext & /*context*/, const rules::Combatant & /*combatant*/) {
    return {};
  }

  void no_tick(const rules::CombatContext & /*context*/, rules::Combatant & /*combatant*/) {}

  void no_toggle(const rules::CombatContext & /*context*/, rules::Combatant & /*combatant*/,
                 std::string_view /*toggle*/) {}

  std::vector<rules::ToggleOption> no_toggles(const rules::CombatContext & /*context*/,
                                              const rules::Combatant & /*combatant*/) {
    return {};
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
        .end_turn = no_turn_end,
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
        .start_turn = no_turn_start,
        .status = no_status,
        .tick = no_tick,
        .toggle = no_toggle,
        .toggles = no_toggles,
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

  /// One tick per step: "a" (index 0, the party) against "b" (index 1, a hostile) under @p combat_rules.
  combat::Encounter start_with(rules::CombatRules combat_rules) {
    std::expected<combat::Encounter, std::string> encounter =
        combat::Encounter::start(std::move(combat_rules), k_context, {party("a"), hostile("b")}, /*steps_per_tick=*/1);
    REQUIRE(encounter.has_value());
    return std::move(*encounter);
  }

  /// The default test rules for "a" and "b", with every Health at 2 and @p plans for the hostile.
  rules::CombatRules duel_rules(rules::CombatantSpec a, rules::CombatantSpec b, Plans plans = {}) {
    return make_rules(Specs{{"a", a}, {"b", b}}, 2, std::move(plans));
  }

  /// Make @p combat_rules log every turn start and end and every tick into @p log, as "<id> starts",
  /// "<id> ends resolved", "<id> ends lost" and "<id> ticks".
  void log_timeline(rules::CombatRules &combat_rules, std::vector<std::string> &log) {
    combat_rules.start_turn = [&log](const rules::CombatContext &, rules::Combatant &combatant) {
      log.push_back(std::format("{} starts", *combatant.creature));
      return std::vector<std::string>{};
    };
    combat_rules.end_turn = [&log](const rules::CombatContext &, rules::Combatant &combatant, rules::TurnEnd how) {
      const std::string_view ending = how == rules::TurnEnd::Resolved ? "resolved" : "lost";
      log.push_back(std::format("{} ends {}", *combatant.creature, ending));
      return rules::TurnEndResult{};
    };
    combat_rules.tick = [&log](const rules::CombatContext &, rules::Combatant &combatant) {
      log.push_back(std::format("{} ticks", *combatant.creature));
    };
  }

  /// End-of-turn rules where "a"'s marker returns to at least @p return_to_beats; "b" keeps its own.
  void lift_returns(rules::CombatRules &combat_rules, int return_to_beats) {
    combat_rules.end_turn = [return_to_beats](const rules::CombatContext &, rules::Combatant &combatant,
                                              rules::TurnEnd) {
      if (combatant.creature != "a")
        return rules::TurnEndResult{};
      return rules::TurnEndResult{.return_to_beats = return_to_beats};
    };
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

  std::ignore = encounter.order_move(k_context, 0);

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

  std::ignore = encounter.order_move(k_context, 0);

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

TEST_CASE("Encounter: a turn starts on the tick the marker reaches the command point") {
  std::vector<std::string> log;
  rules::CombatRules combat_rules = duel_rules(spec(80), spec(0));
  log_timeline(combat_rules, log);
  combat::Encounter encounter = start_with(std::move(combat_rules));
  encounter.order_attack(0, 1);

  std::ignore = encounter.update(k_context, k_adjacent);

  CHECK_EQ(log, std::vector<std::string>{"a starts", "a ticks", "b ticks"});
}

TEST_CASE("Encounter: a turn ends resolved when its command lands") {
  std::vector<std::string> log;
  rules::CombatRules combat_rules = duel_rules(spec(80), spec(0));
  log_timeline(combat_rules, log);
  combat::Encounter encounter = start_with(std::move(combat_rules));
  encounter.order_attack(0, 1);
  std::ignore = encounter.update(k_context, k_adjacent);
  log.clear();

  std::ignore = encounter.update(k_context, k_adjacent);

  CHECK_EQ(log, std::vector<std::string>{"a ticks", "b ticks", "a ends resolved"});
}

TEST_CASE("Encounter: a command that fizzles ends the turn lost") {
  std::vector<std::string> log;
  rules::CombatRules combat_rules = duel_rules(spec(80), spec(0));
  log_timeline(combat_rules, log);
  combat::Encounter encounter = start_with(std::move(combat_rules));
  encounter.order_attack(0, 1);
  std::ignore = encounter.update(k_context, k_adjacent);
  log.clear();

  std::ignore = encounter.update(k_context, k_apart);

  CHECK_EQ(log, std::vector<std::string>{"a ticks", "b ticks", "a ends lost"});
}

TEST_CASE("Encounter: a move order that cancels a wind-up ends the turn lost") {
  std::vector<std::string> log;
  rules::CombatRules combat_rules = duel_rules(spec(80), spec(0));
  log_timeline(combat_rules, log);
  combat::Encounter encounter = start_with(std::move(combat_rules));
  encounter.order_attack(0, 1);
  std::ignore = encounter.update(k_context, k_adjacent);
  log.clear();

  std::ignore = encounter.order_move(k_context, 0);

  CHECK_EQ(log, std::vector<std::string>{"a ends lost"});
}

TEST_CASE("Encounter: a Break ends the target's turn lost before the breaker's turn ends") {
  // Both reach the command point on tick 1. On tick 2 a's bash lands and Breaks b's Smash, which
  // has 10 beats of its 30-beat wind-up left.
  std::vector<std::string> log;
  rules::CombatRules combat_rules = duel_rules(spec(80), spec(80), Plans{{"b", "smash"}});
  log_timeline(combat_rules, log);
  combat::Encounter encounter = start_with(std::move(combat_rules));
  encounter.order_attack(0, 1);
  encounter.order_attack(1, 0);
  encounter.queue_command(0, k_bash);
  std::ignore = encounter.update(k_context, k_adjacent);
  log.clear();

  std::ignore = encounter.update(k_context, k_adjacent);

  CHECK_EQ(log, std::vector<std::string>{"a ticks", "b ticks", "b ends lost", "a ends resolved"});
}

TEST_CASE("Encounter: a marker held at the command point stays in one turn") {
  std::vector<std::string> log;
  rules::CombatRules combat_rules = duel_rules(spec(80), spec(0));
  log_timeline(combat_rules, log);
  combat::Encounter encounter = start_with(std::move(combat_rules));
  encounter.order_attack(0, 1);
  std::ignore = encounter.update(k_context, k_apart);
  std::ignore = encounter.update(k_context, k_apart);
  std::ignore = encounter.update(k_context, k_apart);

  std::ignore = encounter.update(k_context, k_adjacent);

  CHECK_EQ(std::ranges::count(log, std::string{"a starts"}), 1);
  CHECK_EQ(encounter.phase(0), combat::Phase::WindUp);
}

TEST_CASE("Encounter: a turn's end can lift the marker past the command's return point") {
  rules::CombatRules combat_rules = duel_rules(spec(80), spec(0));
  lift_returns(combat_rules, 60);
  combat::Encounter encounter = start_with(std::move(combat_rules));
  encounter.order_attack(0, 1);
  std::ignore = encounter.update(k_context, k_adjacent);

  std::ignore = encounter.update(k_context, k_adjacent);

  CHECK_EQ(encounter.beats(0), 60);
}

TEST_CASE("Encounter: a turn's end never lowers a return point already further along") {
  rules::CombatRules combat_rules = duel_rules(spec(80, 0, /*return_to=*/80), spec(0));
  lift_returns(combat_rules, 60);
  combat::Encounter encounter = start_with(std::move(combat_rules));
  encounter.order_attack(0, 1);
  std::ignore = encounter.update(k_context, k_adjacent);

  std::ignore = encounter.update(k_context, k_adjacent);

  CHECK_EQ(encounter.beats(0), 80);
}

TEST_CASE("Encounter: a return to the command point starts the next turn on the next tick") {
  rules::CombatRules combat_rules = duel_rules(spec(80), spec(0));
  lift_returns(combat_rules, 100);
  combat::Encounter encounter = start_with(std::move(combat_rules));
  encounter.order_attack(0, 1);
  std::ignore = encounter.update(k_context, k_adjacent);
  std::ignore = encounter.update(k_context, k_adjacent);
  REQUIRE_EQ(encounter.phase(0), combat::Phase::Ready);
  REQUIRE_EQ(encounter.beats(0), 100);

  std::ignore = encounter.update(k_context, k_adjacent);

  CHECK_EQ(encounter.phase(0), combat::Phase::WindUp);
  CHECK_EQ(encounter.beats(0), 100);
}

TEST_CASE("Encounter: a turn's end reports follow its command's report") {
  rules::CombatRules combat_rules = duel_rules(spec(80), spec(0));
  combat_rules.end_turn = [](const rules::CombatContext &, rules::Combatant &combatant, rules::TurnEnd) {
    return rules::TurnEndResult{.reports = {std::format("{} ends", *combatant.creature)}};
  };
  combat::Encounter encounter = start_with(std::move(combat_rules));
  encounter.order_attack(0, 1);
  std::ignore = encounter.update(k_context, k_adjacent);

  const combat::EncounterStep step = encounter.update(k_context, k_adjacent);

  CHECK_EQ(step.reports, std::vector<std::string>{"a hits b", "a ends"});
}

TEST_CASE("Encounter: order_move returns the end-of-turn reports of the wind-up it cancels") {
  rules::CombatRules combat_rules = duel_rules(spec(80), spec(0));
  combat_rules.end_turn = [](const rules::CombatContext &, rules::Combatant &combatant, rules::TurnEnd) {
    return rules::TurnEndResult{.reports = {std::format("{} ends", *combatant.creature)}};
  };
  combat::Encounter encounter = start_with(std::move(combat_rules));
  encounter.order_attack(0, 1);
  std::ignore = encounter.update(k_context, k_adjacent);

  const std::vector<std::string> reports = encounter.order_move(k_context, 0);

  CHECK_EQ(reports, std::vector<std::string>{"a ends"});
}

TEST_CASE("Encounter: a turn start that leaves the combatant unable to act starts no command") {
  rules::CombatRules combat_rules = duel_rules(spec(80), spec(0));
  combat_rules.start_turn = [](const rules::CombatContext &, rules::Combatant &combatant) {
    combatant.state[k_health] = 0;
    return std::vector<std::string>{std::format("{} burns", *combatant.creature)};
  };
  combat::Encounter encounter = start_with(std::move(combat_rules));
  encounter.order_attack(0, 1);

  const combat::EncounterStep step = encounter.update(k_context, k_adjacent);

  CHECK_EQ(step.reports, std::vector<std::string>{"a burns"});
  CHECK_EQ(encounter.phase(0), combat::Phase::Ready);
  CHECK_EQ(encounter.outcome(), std::optional<combat::Outcome>{combat::Outcome::Lost});
}

TEST_CASE("Encounter: a marker its status holds doesn't move") {
  rules::CombatRules combat_rules = duel_rules(spec(40), spec(0));
  combat_rules.status = [](const rules::CombatContext &, const rules::Combatant &combatant) {
    return rules::CombatantStatus{.can_advance = combatant.creature != "a"};
  };
  combat::Encounter encounter = start_with(std::move(combat_rules));

  std::ignore = encounter.update(k_context, k_adjacent);

  CHECK_EQ(encounter.beats(0), 40);
  CHECK_EQ(encounter.beats(1), 20);
}

TEST_CASE("Encounter: a held combatant still hears every tick") {
  std::vector<std::string> log;
  rules::CombatRules combat_rules = duel_rules(spec(40), spec(0));
  log_timeline(combat_rules, log);
  combat_rules.status = [](const rules::CombatContext &, const rules::Combatant &combatant) {
    return rules::CombatantStatus{.can_advance = combatant.creature != "a"};
  };
  combat::Encounter encounter = start_with(std::move(combat_rules));

  std::ignore = encounter.update(k_context, k_adjacent);

  CHECK_EQ(log, std::vector<std::string>{"a ticks", "b ticks"});
}

TEST_CASE("Encounter: every status is asked again after a tick") {
  rules::CombatRules combat_rules = duel_rules(spec(80), spec(0));
  combat_rules.status = [](const rules::CombatContext &, const rules::Combatant &combatant) {
    return rules::CombatantStatus{.labels = {std::to_string(combatant.state.find(k_health)->second)}};
  };
  combat::Encounter encounter = start_with(std::move(combat_rules));
  REQUIRE_EQ(encounter.status(1).labels, std::vector<std::string>{"2"});
  encounter.order_attack(0, 1);
  std::ignore = encounter.update(k_context, k_adjacent);

  std::ignore = encounter.update(k_context, k_adjacent);

  CHECK_EQ(encounter.status(1).labels, std::vector<std::string>{"1"});
}

TEST_CASE("Encounter: a toggle goes through the rules, then the toggles and the status refresh") {
  constexpr std::string_view k_focus = "focus";
  rules::CombatRules combat_rules = duel_rules(spec(0), spec(0));
  combat_rules.toggles = [k_focus](const rules::CombatContext &, const rules::Combatant &combatant) {
    return std::vector<rules::ToggleOption>{
        {.id = "focus", .is_available = true, .is_on = combatant.state.contains(k_focus), .label = "Focus"},
    };
  };
  combat_rules.toggle = [k_focus](const rules::CombatContext &, rules::Combatant &combatant, std::string_view) {
    combatant.state[std::string{k_focus}] = 1;
  };
  combat_rules.status = [k_focus](const rules::CombatContext &, const rules::Combatant &combatant) {
    return rules::CombatantStatus{
        .labels = combatant.state.contains(k_focus) ? std::vector<std::string>{"Focus"} : std::vector<std::string>{},
    };
  };
  combat::Encounter encounter = start_with(std::move(combat_rules));

  encounter.toggle(k_context, 0, 0);

  CHECK(encounter.toggles(0)[0].is_on);
  CHECK_EQ(encounter.status(0).labels, std::vector<std::string>{"Focus"});
}

TEST_CASE("Encounter::start refuses rules with no status callback") {
  rules::CombatRules incomplete = duel_rules(spec(0), spec(0));
  incomplete.status = {};

  const std::expected<combat::Encounter, std::string> encounter =
      combat::Encounter::start(std::move(incomplete), k_context, {party("a"), hostile("b")}, /*steps_per_tick=*/1);

  REQUIRE_FALSE(encounter.has_value());
  CHECK_EQ(encounter.error(), "[gameplay] the combat rules have no 'status' callback");
}

TEST_CASE("Encounter: windup_seconds counts whole ticks from the command point") {
  combat::Encounter encounter = start_duel(spec(80), spec(0));
  encounter.order_attack(0, 1);
  encounter.queue_command(0, k_smash);

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
  combat::Encounter encounter = start_duel(spec(95), spec(0));
  encounter.order_attack(0, 1);
  encounter.queue_command(0, k_smash);

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

TEST_CASE("Encounter: an Enemy command winds up at an enemy") {
  combat::Encounter encounter = start_duel(spec(80), spec(0));
  encounter.order_attack(0, 1);
  encounter.queue_command(0, k_jab);

  std::ignore = encounter.update(k_context, k_adjacent);

  CHECK(encounter.is_winding_up_at_enemy(0));
}

TEST_CASE("Encounter: a Self command doesn't wind up at an enemy") {
  combat::Encounter encounter = start_duel(spec(80), spec(0));
  encounter.queue_command(0, k_brace);

  std::ignore = encounter.update(k_context, k_adjacent);

  REQUIRE_EQ(encounter.windup_label(0), "Brace");
  CHECK_FALSE(encounter.is_winding_up_at_enemy(0));
}

TEST_CASE("Encounter: a combatant not winding up isn't winding up at an enemy") {
  const combat::Encounter encounter = start_duel(spec(0), spec(0));

  CHECK_FALSE(encounter.is_winding_up_at_enemy(0));
}
