// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The seam between the gameplay framework and a game's rules. The framework owns the scene: where
// each combatant stands, what the player ordered, which clip plays and what the HUD shows. The
// ruleset owns the rules: the character sheet's values, and each fight from who acts when to what
// a blow does. A ruleset keeps the protagonist's lasting state in the playthrough's flags under
// its own key prefix, so saves, loads and playthrough boundaries carry it with no hook of their
// own; it reads and writes that state through FlagReader and FlagWriter, never the FlagStore
// itself. State that lasts one fight lives behind the ruleset's Fight and ends with it. This
// header is part of corundum::rules_interface and includes no other corundum header, so a ruleset
// builds against that target alone.
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

  /** @brief A map tile, by column and row. */
  struct Tile {
    int col{};

    int row{};

    bool operator==(const Tile &) const = default;
  };

  /** @brief Steps between two tiles when a diagonal step counts as one: the distance
   *  Fight::reach_tiles counts, and the one the framework walks a combatant into reach by. */
  [[nodiscard]] inline int tile_distance(Tile a, Tile b) noexcept {
    return std::max(std::abs(a.col - b.col), std::abs(a.row - b.row));
  }

  /** @brief A command a combatant can order besides its standing attack, such as drinking a
   *  potion. */
  struct CommandOption {
    std::string id{}; ///< The ruleset's own name for it; the framework never reads it.

    bool is_available{true}; ///< False while it can't be ordered (none left).

    std::string label{}; ///< Shown on the command bar, such as "Herbal Tonic (2)".

    bool operator==(const CommandOption &) const = default;
  };

  /** @brief A combatant's Health as the HUD shows it. */
  struct Health {
    int current{};

    int max{};

    bool operator==(const Health &) const = default;
  };

  /** @brief How a fight ended, from the party's side. */
  enum class Outcome : std::uint8_t { Won, Lost };

  /** @brief A command a combatant is winding up. */
  struct WindUp {
    bool is_attack{}; ///< The standing attack on its target; false for one of its commands.

    float seconds_left{}; ///< Game time until the command lands.
  };

  /** @brief What one Fight::update produced. */
  struct FightStep {
    std::vector<std::string> reports{}; ///< One line per command that landed, in order, for the combat log.

    std::vector<std::size_t> windups_started{}; ///< Combatants whose wind-up started this step, by index.
  };

  /** @brief The fight the framework asks a ruleset to run. */
  struct FightRequest {
    std::vector<std::string> creature_ids{}; ///< The hostiles, in order: combatant i + 1 is creature_ids[i].

    float step_seconds{}; ///< Game time per Fight::update.
  };

  /** @brief One fight as its ruleset runs it.
   *
   *  Combatant 0 is the protagonist and combatant i + 1 is the request's creature_ids[i]; every
   *  index a callback takes or returns is one of those. The callbacks share the ruleset's state for
   *  this fight, so a copy of a Fight is the same fight. Every callback must be set.
   *
   *  The framework orders only the protagonist, when the player's input calls for it. Who a
   *  creature attacks is the ruleset's choice, made by the time start_fight returns and read back
   *  through target_of. The framework calls update once per fixed step while the fight runs, and
   *  asks the queries several times per step, so a query only reads the fight's state.
   */
  struct Fight {
    std::vector<std::string> names{}; ///< Each combatant's name on the combat HUD, by index.

    int reach_tiles{1}; ///< Tiles a standing attack reaches, by tile_distance; must be positive.

    /** Drop @p actor's queued command, so its standing attack comes next. */
    std::function<void(std::size_t actor)> clear_queued{};

    /** Give @p attacker a standing attack on @p target, an enemy, replacing any order. */
    std::function<void(std::size_t attacker, std::size_t target)> order_attack{};

    /** @p mover walks on its own: drop its attack order. */
    std::function<void(std::size_t mover)> order_move{};

    /** Queue commands(actor)[@p option], which is available, in place of @p actor's next standing
     *  attack. */
    std::function<void(std::size_t actor, std::size_t option)> queue_command{};

    /** Advance one fixed step of FightRequest::step_seconds. @p tiles holds each combatant's tile,
     *  by index. */
    std::function<FightStep(const CombatContext &, std::span<const Tile> tiles)> update{};

    /** Whether the combatant can still fight; one that can't leaves the scene. */
    std::function<bool(const CombatContext &, std::size_t index)> can_act{};

    /** The commands @p actor can order besides its standing attack, in command-bar order. The list
     *  keeps its length for the whole fight; only is_available and label change. The span must
     *  stay valid until the next update; the framework keeps it no longer. */
    std::function<std::span<const CommandOption>(std::size_t actor)> commands{};

    std::function<Health(const CombatContext &, std::size_t index)> health{};

    /** How the fight ended; nullopt while it runs. */
    std::function<std::optional<Outcome>()> outcome{};

    /** @p actor's queued command, as an index into commands(actor); nullopt for none. */
    std::function<std::optional<std::size_t>(std::size_t actor)> queued{};

    /** The combatant @p attacker has a standing attack on; nullopt for none. */
    std::function<std::optional<std::size_t>(std::size_t attacker)> target_of{};

    /** The command @p index is winding up; nullopt while it winds none up. */
    std::function<std::optional<WindUp>(std::size_t index)> windup{};
  };

  /** @brief Start a fight between the protagonist and the request's creatures, or return why it
   *  can't start (an unknown creature, no weapon). */
  using StartFight = std::function<std::expected<Fight, std::string>(const CombatContext &, const FightRequest &)>;

  /** @brief A game's rules, as callbacks the framework calls when it needs a value.
   *
   *  A struct of std::function rather than a virtual interface, matching the framework's other
   *  game hooks (Gameplay::on_event). A callback may be empty, and the framework then shows
   *  nothing for it. A callback reads only what it is passed.
   */
  struct Ruleset {
    /** @brief The protagonist's rows for the character sheet's Stats column, in display order. */
    std::function<std::vector<StatRow>(const FlagReader &)> character_rows{};

    /** @brief The game's combat; empty when it has none, and start_encounter then warns and starts
     *  nothing. */
    StartFight start_fight{};
  };

} // namespace corundum::gameplay::rules
