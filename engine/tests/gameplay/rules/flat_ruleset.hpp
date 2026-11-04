// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/gameplay/rules/rules.hpp>

#include <algorithm>
#include <expected>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace corundum::test {

  /// The minimal ruleset the framework is tested against: one Health row read verbatim from the
  /// `test.health` and `test.max_health` flags. It holds no game-specific rule.
  inline gameplay::rules::Ruleset make_flat_ruleset() {
    return gameplay::rules::Ruleset{
        .character_rows =
            [](const gameplay::rules::FlagReader &reader) {
              return std::vector<gameplay::rules::StatRow>{
                  {
                      .label = "Health",
                      .value = std::format("{} / {}", reader.read("test.health"), reader.read("test.max_health")),
                  },
              };
            },
    };
  }

  inline constexpr std::string_view k_flat_health_key = "health";

  inline constexpr int k_flat_max_health = 2;

  /// The minimal combat the framework is tested against, one tick per 60 Hz step.
  ///
  /// The protagonist ("hero") starts at the `test.hero.start` flag's beats with 2 Health less the
  /// `test.hero.damage` flag. Its commands are "bash" (label "Bash": at an enemy, a 20-beat wind-up
  /// returning to 0; it Breaks) and "tonic" (label "Tonic": on itself, 10 beats returning to 40;
  /// available while the `test.hero.tonics` flag is above 0, it uses one up and heals 1).
  ///
  /// A creature, named by its id, starts at 0 beats with 2 Health in its state and has no
  /// commands, except "brute": its one command, "smash" (label "Smash", a 30-beat heavy wind-up
  /// returning to 0), is the one it always chooses. Everyone has reach 1 and a 20-beat standing
  /// attack returning to 0. Every hit lands for 1, reporting "<attacker> hits <target>", or
  /// "<attacker> uses <command> on <target>" for a command. The fight auto-pauses on a hostile
  /// wind-up of 30 beats or more within 3 tiles. The creature id "missing" fails to join.
  ///
  /// The hero's one toggle is "guard" (label "Guard"), on while the `test.hero.guard` flag is above
  /// 0; toggling flips the flag. The hero can't move while the `test.hero.rooted` flag is above 0,
  /// nor can the creature "statue"; the hero's status labels are {"Guard"} while guarded. Turns and
  /// ticks do nothing.
  inline gameplay::rules::CombatRules make_flat_combat_rules() {
    using gameplay::rules::CombatContext;
    using gameplay::rules::Combatant;
    using gameplay::rules::CombatantSpec;
    using gameplay::rules::CommandOption;
    using gameplay::rules::CommandResult;
    using gameplay::rules::CommandTarget;
    const auto name_of = [](const Combatant &combatant) { return combatant.creature.value_or("hero"); };
    const auto health_of = [](const CombatContext &context, const Combatant &combatant) {
      if (!combatant.creature)
        return gameplay::rules::Health{
            .current = k_flat_max_health - context.flags.read("test.hero.damage"),
            .max = k_flat_max_health,
        };
      const auto found = combatant.state.find(k_flat_health_key);
      return gameplay::rules::Health{
          .current = found != combatant.state.end() ? found->second : 0,
          .max = k_flat_max_health,
      };
    };
    const auto hero_commands = [](const CombatContext &context) {
      return std::vector<CommandOption>{
          {
              .id = "bash",
              .is_available = true,
              .label = "Bash",
              .target = CommandTarget::Enemy,
              .timing = {.return_to_beats = 0, .windup_beats = 20},
          },
          {
              .id = "tonic",
              .is_available = context.flags.read("test.hero.tonics") > 0,
              .label = "Tonic",
              .target = CommandTarget::Self,
              .timing = {.return_to_beats = 40, .windup_beats = 10},
          },
      };
    };
    const CommandOption smash{
        .id = "smash",
        .is_available = true,
        .label = "Smash",
        .target = CommandTarget::Enemy,
        .timing = {.return_to_beats = 0, .windup_beats = 30},
    };
    return gameplay::rules::CombatRules{
        .auto_pause = gameplay::rules::AutoPause{.max_distance_tiles = 3, .min_windup_beats = 30},
        .break_return_beats = 0,
        .command_point_beats = 100,
        .seconds_per_tick = 1.f / 60.f,
        .tempo_beats = 20,
        .can_act = [health_of](const CombatContext &context,
                               const Combatant &combatant) { return health_of(context, combatant).current > 0; },
        .commands =
            [hero_commands, smash](const CombatContext &context, const Combatant &combatant) {
              if (!combatant.creature)
                return hero_commands(context);
              if (combatant.creature == "brute")
                return std::vector<CommandOption>{smash};
              return std::vector<CommandOption>{};
            },
        .end_turn = [](const CombatContext &, Combatant &,
                       gameplay::rules::TurnEnd) { return gameplay::rules::TurnEndResult{}; },
        .health = health_of,
        .hostile_command = [smash](const CombatContext &, Combatant &hostile) -> std::optional<CommandOption> {
          if (hostile.creature == "brute")
            return smash;
          return std::nullopt;
        },
        .join = [](const CombatContext &context, Combatant &combatant) -> std::expected<CombatantSpec, std::string> {
          if (combatant.creature == "missing")
            return std::unexpected(std::string{"no creature 'missing'"});
          const gameplay::rules::Command attack{.return_to_beats = 0, .windup_beats = 20};
          if (!combatant.creature) {
            return CombatantSpec{
                .attack = attack,
                .initiative = 0,
                .name = "hero",
                .reach_tiles = 1,
                .start_beats = context.flags.read("test.hero.start"),
            };
          }
          combatant.state[k_flat_health_key] = k_flat_max_health;
          return CombatantSpec{
              .attack = attack,
              .initiative = 0,
              .name = *combatant.creature,
              .reach_tiles = 1,
              .start_beats = 0,
          };
        },
        .resolve =
            [name_of](const CombatContext &context, std::string_view command, Combatant &actor, Combatant &target) {
              if (target.creature)
                target.state[k_flat_health_key] -= 1;
              else
                context.flag_writer.write("test.hero.damage", context.flags.read("test.hero.damage") + 1);
              const std::string verb = command.empty() ? "hits" : std::format("uses {} on", command);
              return CommandResult{
                  .breaks = command == "bash",
                  .report = std::format("{} {} {}", name_of(actor), verb, name_of(target)),
                  .stagger_beats = 0,
              };
            },
        .resolve_self =
            [](const CombatContext &context, std::string_view, Combatant &) {
              context.flag_writer.write("test.hero.tonics", context.flags.read("test.hero.tonics") - 1);
              context.flag_writer.write("test.hero.damage", std::max(0, context.flags.read("test.hero.damage") - 1));
              return std::string{"hero drinks a tonic"};
            },
        .start_turn = [](const CombatContext &, Combatant &) { return std::vector<std::string>{}; },
        .status =
            [](const CombatContext &context, const Combatant &combatant) {
              if (combatant.creature)
                return gameplay::rules::CombatantStatus{.can_move = combatant.creature != "statue"};
              const bool is_guarded = context.flags.read("test.hero.guard") > 0;
              return gameplay::rules::CombatantStatus{
                  .can_advance = true,
                  .can_move = context.flags.read("test.hero.rooted") == 0,
                  .labels = is_guarded ? std::vector<std::string>{"Guard"} : std::vector<std::string>{},
              };
            },
        .tick = [](const CombatContext &, Combatant &) {},
        .toggle =
            [](const CombatContext &context, Combatant &, std::string_view) {
              context.flag_writer.write("test.hero.guard", context.flags.read("test.hero.guard") > 0 ? 0 : 1);
            },
        .toggles =
            [](const CombatContext &context, const Combatant &combatant) {
              if (combatant.creature)
                return std::vector<gameplay::rules::ToggleOption>{};
              return std::vector<gameplay::rules::ToggleOption>{
                  {
                      .id = "guard",
                      .is_available = true,
                      .is_on = context.flags.read("test.hero.guard") > 0,
                      .label = "Guard",
                  },
              };
            },
    };
  }

} // namespace corundum::test
