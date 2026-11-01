// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <expected>
#include <flat_map>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The seam between the gameplay framework and a game's rules. The framework decides when a value
// is needed, when a fight runs and how values and the fight are shown; the ruleset decides what
// they are. A ruleset keeps the protagonist's state in the playthrough's flags under its own key
// prefix, so saves, loads and playthrough boundaries carry it with no hook of their own; it reads
// and writes that state through FlagReader and FlagWriter, never the FlagStore itself. A creature's
// state in a fight lives in its Combatant and ends with the encounter. This header is part of
// corundum::rules_interface and includes no other corundum header, so a ruleset builds against
// that target alone.
namespace corundum::gameplay::rules {

  /** @brief One "label: value" row a ruleset supplies for display, such as {"Strength", "+2"}. */
  struct StatRow {
    std::string label{};

    std::string value{};

    bool operator==(const StatRow &) const = default;
  };

  /** @brief Reads the playthrough's integer state by FlagStore key; an absent key reads as 0.
   *  @note Valid only for the duration of the ruleset call it is passed to; never store it. */
  struct FlagReader {
    std::function<int(std::string_view key)> read{};
  };

  /** @brief Sets the playthrough's integer state by FlagStore key.
   *  @note Valid only for the duration of the ruleset call it is passed to; never store it. */
  struct FlagWriter {
    std::function<void(std::string_view key, int value)> write{};
  };

  /** @brief Rolls one die: `roll(sides)` returns 1 to @c sides, drawn from the engine's seeded
   *  generator so a run replays from its seed.
   *  @pre sides >= 1.
   *  @note Valid only for the duration of the ruleset call it is passed to; never store it. */
  struct Dice {
    std::function<int(int sides)> roll{};
  };

  /** @brief What a combat callback may use: dice and the playthrough's state. */
  struct CombatContext {
    Dice dice{};

    FlagWriter flag_writer{};

    FlagReader flags{};
  };

  /** @brief A combatant's integer state for one encounter, under keys the ruleset chooses. */
  using CombatantState = std::flat_map<std::string, int, std::less<>>;

  /** @brief One combatant as the ruleset sees it. */
  struct Combatant {
    /** The creature id the game started the encounter with; nullopt for the protagonist, whose
     *  state lives in the flags instead of @c state. */
    std::optional<std::string> creature{};

    CombatantState state{};
  };

  /** @brief A command's timing on the timeline, in beats. */
  struct Command {
    int return_to_beats{}; ///< Where the marker drops once the command lands or fizzles.

    int windup_beats{}; ///< Beats past the command point before the command lands.
  };

  /** @brief A combatant's fixed numbers for one encounter, from CombatRules::join. */
  struct CombatantSpec {
    Command attack{}; ///< The standing attack.

    int initiative{}; ///< Breaks a tie between attacks equally far along; higher goes first.

    std::string name{}; ///< Shown on the combat HUD.

    int reach_tiles{1}; ///< Tiles the standing attack reaches; a diagonal step counts as one.

    int start_beats{}; ///< Where the marker starts.
  };

  /** @brief A combatant's Health as the HUD shows it. */
  struct Health {
    int current{};

    int max{};

    bool operator==(const Health &) const = default;
  };

  /** @brief A ruleset's combat: the timeline's constants and the callbacks that decide each
   *  combatant's numbers and each attack. Every callback must be set. */
  struct CombatRules {
    int command_point_beats{}; ///< Where a marker's command is carried out; must be positive.

    float seconds_per_tick{}; ///< Game time per timeline tick.

    int tempo_beats{}; ///< Beats a marker gains per tick; must be positive.

    /** The standing attack of @p attacker on @p target, already in reach: roll, apply the result
     *  to @p target's state (or, for the protagonist, the flags) and return a one-line report. */
    std::function<std::string(const CombatContext &, const Combatant &attacker, Combatant &target)> attack{};

    /** Whether the combatant can still fight; the fight ends when one side has nobody who can. */
    std::function<bool(const CombatContext &, const Combatant &)> can_act{};

    std::function<Health(const CombatContext &, const Combatant &)> health{};

    /** The combatant's numbers for this encounter, rolling initiative and setting up its state; an
     *  error (an unknown creature, no weapon) stops the fight from starting. */
    std::function<std::expected<CombatantSpec, std::string>(const CombatContext &, Combatant &)> join{};
  };

  /** @brief A game's rules, as callbacks the framework calls when it needs a value.
   *
   *  A struct of std::function rather than a virtual interface, matching the framework's other
   *  game hooks (Gameplay::on_event). A callback may be empty, and the framework then shows
   *  nothing for it. A callback reads only what it is passed.
   */
  struct Ruleset {
    /** @brief The protagonist's rows for the character sheet's Stats column, in display order. */
    std::function<std::vector<StatRow>(const FlagReader &)> character_rows{};

    /** @brief The game's combat, or nullopt when it has none; start_encounter then warns and starts
     *  nothing. */
    std::optional<CombatRules> combat{};
  };

} // namespace corundum::gameplay::rules
