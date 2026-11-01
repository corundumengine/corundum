// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/gameplay/rules/rules.hpp>

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

  /// The minimal combat the framework is tested against, one tick per 60 Hz step. The protagonist
  /// ("hero") starts at the `test.hero.start` flag's beats with 2 Health less the `test.hero.damage`
  /// flag; a creature, named by its id, starts at 0 beats with 2 Health in its state. Everyone has
  /// reach 1 and a 20-beat wind-up returning to 0, and every attack lands for 1, reporting
  /// "<attacker> hits <target>". The creature id "missing" fails to join.
  inline gameplay::rules::CombatRules make_flat_combat_rules() {
    using gameplay::rules::CombatContext;
    using gameplay::rules::Combatant;
    using gameplay::rules::CombatantSpec;
    const auto name_of = [](const Combatant &combatant) { return combatant.creature.value_or("hero"); };
    const auto health_of = [](const CombatContext &context, const Combatant &combatant) {
      if (!combatant.creature)
        return gameplay::rules::Health{
            .current = k_flat_max_health - context.flags.read("test.hero.damage"), .max = k_flat_max_health,
        };
      const auto found = combatant.state.find(k_flat_health_key);
      return gameplay::rules::Health{
          .current = found != combatant.state.end() ? found->second : 0, .max = k_flat_max_health,
      };
    };
    return gameplay::rules::CombatRules{
        .command_point_beats = 100,
        .seconds_per_tick = 1.f / 60.f,
        .tempo_beats = 20,
        .attack =
            [name_of](const CombatContext &context, const Combatant &attacker, Combatant &target) {
              if (target.creature)
                target.state[k_flat_health_key] -= 1;
              else
                context.flag_writer.write("test.hero.damage", context.flags.read("test.hero.damage") + 1);
              return std::format("{} hits {}", name_of(attacker), name_of(target));
            },
        .can_act = [health_of](const CombatContext &context,
                               const Combatant &combatant) { return health_of(context, combatant).current > 0; },
        .health = health_of,
        .join = [](const CombatContext &context, Combatant &combatant) -> std::expected<CombatantSpec, std::string> {
          if (combatant.creature == "missing")
            return std::unexpected(std::string{"no creature 'missing'"});
          const gameplay::rules::Command attack{.return_to_beats = 0, .windup_beats = 20};
          if (!combatant.creature) {
            return CombatantSpec{.attack = attack,
                                 .initiative = 0,
                                 .name = "hero",
                                 .reach_tiles = 1,
                                 .start_beats = context.flags.read("test.hero.start"),
            };
          }
          combatant.state[k_flat_health_key] = k_flat_max_health;
          return CombatantSpec{
              .attack = attack, .initiative = 0, .name = *combatant.creature, .reach_tiles = 1, .start_beats = 0,
          };
        },
    };
  }

} // namespace corundum::test
