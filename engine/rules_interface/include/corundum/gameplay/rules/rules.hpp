// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <expected>
#include <flat_map>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The seam between the gameplay framework and a game's rules. The framework decides when a value
// is needed, when a fight runs and how values and the fight are shown; the ruleset decides what
// they are. A ruleset keeps the protagonist's lasting state in the playthrough's flags under its
// own key prefix, so saves, loads and playthrough boundaries carry it with no hook of their own;
// it reads and writes that state through FlagReader and FlagWriter, never the FlagStore itself.
// State that lasts one encounter (a creature's damage, a command's uses this fight) lives in the
// Combatant and ends with the encounter. This header is part of corundum::rules_interface and
// includes no other corundum header, so a ruleset builds against that target alone.
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
     *  lasting state lives in the flags. */
    std::optional<std::string> creature{};

    CombatantState state{}; ///< This encounter's state, for the protagonist as for a creature.
  };

  /** @brief A command's timing on the timeline, in beats. */
  struct Command {
    int return_to_beats{}; ///< Where the marker drops once the command lands or fizzles.

    int windup_beats{}; ///< Beats past the command point before the command lands.

    bool operator==(const Command &) const = default;
  };

  /** @brief Who a command is aimed at. */
  enum class CommandTarget : std::uint8_t {
    Enemy, ///< The combatant's attack target, which must be in reach when the wind-up ends.
    Self,  ///< The combatant itself; it needs no target and no reach.
  };

  /** @brief A command a combatant can order besides its standing attack: an ability, or an item to
   *  use. */
  struct CommandOption {
    std::string id{}; ///< The ruleset's name for it, passed back to resolve or resolve_self.

    bool is_available{true}; ///< False while it can't be ordered (none left, spent this fight).

    std::string label{}; ///< Shown on the command bar and in telegraphs, such as "Shield Bash".

    CommandTarget target{CommandTarget::Enemy};

    Command timing{};

    bool operator==(const CommandOption &) const = default;
  };

  /** @brief What a command aimed at an enemy did. */
  struct CommandResult {
    /** Break the target's wind-up, if it is winding up: the command is lost and the marker drops
     *  to CombatRules::break_return_beats. */
    bool breaks{};

    std::string report{}; ///< One line for the combat log, such as "You → Brann: hit, 4 damage".

    int stagger_beats{}; ///< Beats the target's marker is knocked back, never below 0; 0 for none.

    bool operator==(const CommandResult &) const = default;
  };

  /** @brief When a hostile's wind-up pauses the fight on its own: one at least min_windup_beats
   *  long, started within max_distance_tiles of the protagonist. */
  struct AutoPause {
    int max_distance_tiles{};

    int min_windup_beats{};
  };

  /** @brief A combatant's fixed numbers for one encounter, from CombatRules::join. */
  struct CombatantSpec {
    Command attack{}; ///< The standing attack.

    int initiative{}; ///< Breaks a tie between attacks equally far along; higher goes first.

    std::string name{}; ///< Shown on the combat HUD.

    int reach_tiles{1}; ///< Tiles the combatant's Enemy commands reach; a diagonal step counts as one.

    int start_beats{}; ///< Where the marker starts.
  };

  /** @brief A combatant's Health as the HUD shows it. */
  struct Health {
    int current{};

    int max{};

    bool operator==(const Health &) const = default;
  };

  /** @brief A ruleset's combat: the timeline's constants and the callbacks that decide each
   *  combatant's numbers, commands and results. Every callback must be set. */
  struct CombatRules {
    std::optional<AutoPause> auto_pause{}; ///< nullopt: the fight never pauses on its own.

    int break_return_beats{}; ///< Where a Broken wind-up's marker drops.

    int command_point_beats{}; ///< Where a marker's command is carried out; must be positive.

    float seconds_per_tick{}; ///< Game time per timeline tick.

    int tempo_beats{}; ///< Beats a marker gains per tick; must be positive.

    /** Whether the combatant can still fight; the fight ends when one side has nobody who can. */
    std::function<bool(const CombatContext &, const Combatant &)> can_act{};

    /** The commands the combatant can order besides its standing attack, in command-bar order. The
     *  same commands in the same order for the whole encounter; only is_available and a label's
     *  count may change. The framework asks again after every command resolves. */
    std::function<std::vector<CommandOption>(const CombatContext &, const Combatant &)> commands{};

    std::function<Health(const CombatContext &, const Combatant &)> health{};

    /** The command a hostile winds up now that it is at its command point with its target in
     *  reach: one of its commands(), or nullopt for its standing attack. May advance a pattern kept
     *  in @p hostile's state. */
    std::function<std::optional<CommandOption>(const CombatContext &, Combatant &hostile)> hostile_command{};

    /** The combatant's numbers for this encounter, rolling initiative and setting up its state; an
     *  error (an unknown creature, no weapon) stops the fight from starting. */
    std::function<std::expected<CombatantSpec, std::string>(const CombatContext &, Combatant &)> join{};

    /** Carry out @p command (empty for the standing attack) by @p actor on @p target, already in
     *  reach: roll, apply the damage to @p target's state (or, for the protagonist, the flags), and
     *  say what happened. @p actor and @p target are different combatants. */
    std::function<CommandResult(const CombatContext &, std::string_view command, Combatant &actor, Combatant &target)>
        resolve{};

    /** Carry out the Self command @p command by @p actor and return a one-line report. */
    std::function<std::string(const CombatContext &, std::string_view command, Combatant &actor)> resolve_self{};
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
