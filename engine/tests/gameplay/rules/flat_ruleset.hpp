// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/gameplay/rules/rules.hpp>

#include "gameplay/rules/flat_timeline.hpp"

#include <algorithm>
#include <expected>
#include <format>
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
  /// `test.hero.damage` flag. Its commands are "focus" (label "Focus": always available, does
  /// nothing), "tonic" (label "Tonic": available while the `test.hero.tonics` flag is above 0, it
  /// uses one up and heals 1) and "salve" (label "Salve": always available, does nothing).
  ///
  /// A creature, named by its id, starts at 0 beats with 2 Health in its state and has no
  /// commands. Reach is 1 and every wind-up 20 beats. Every attack lands for 1, reporting
  /// "<attacker> hits <target>". The creature id "missing" fails to join.
  inline CombatRules make_flat_combat_rules() {
    using gameplay::rules::CombatContext;
    using gameplay::rules::CommandOption;
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
          {.id = "focus", .is_available = true, .label = "Focus"},
          {.id = "tonic", .is_available = context.flags.read("test.hero.tonics") > 0, .label = "Tonic"},
          {.id = "salve", .is_available = true, .label = "Salve"},
      };
    };
    return CombatRules{
        .command_point_beats = 100,
        .reach_tiles = 1,
        .seconds_per_tick = 1.f / 60.f,
        .tempo_beats = 20,
        .windup_beats = 20,
        .can_act = [health_of](const CombatContext &context,
                               const Combatant &combatant) { return health_of(context, combatant).current > 0; },
        .commands =
            [hero_commands](const CombatContext &context, const Combatant &combatant) {
              if (combatant.creature)
                return std::vector<CommandOption>{};
              return hero_commands(context);
            },
        .health = health_of,
        .join = [](const CombatContext &context, Combatant &combatant) -> std::expected<CombatantSpec, std::string> {
          if (combatant.creature == "missing")
            return std::unexpected(std::string{"no creature 'missing'"});
          if (!combatant.creature)
            return CombatantSpec{.name = "hero", .start_beats = context.flags.read("test.hero.start")};
          combatant.state[k_flat_health_key] = k_flat_max_health;
          return CombatantSpec{.name = *combatant.creature, .start_beats = 0};
        },
        .resolve =
            [name_of](const CombatContext &context, Combatant &actor, Combatant &target) {
              if (target.creature)
                target.state[k_flat_health_key] -= 1;
              else
                context.flag_writer.write("test.hero.damage", context.flags.read("test.hero.damage") + 1);
              return std::format("{} hits {}", name_of(actor), name_of(target));
            },
        .resolve_self =
            [](const CombatContext &context, std::string_view command, Combatant &) {
              if (command != "tonic")
                return std::format("hero uses {}", command);
              context.flag_writer.write("test.hero.tonics", context.flags.read("test.hero.tonics") - 1);
              context.flag_writer.write("test.hero.damage", std::max(0, context.flags.read("test.hero.damage") - 1));
              return std::string{"hero drinks a tonic"};
            },
    };
  }

} // namespace corundum::test
