// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/gameplay/rules/rules.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <flat_map>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The combat timeline the framework's tests run fights on: a test double for a ruleset's Fight.
// Every combatant's marker fills toward the command point, winds up a command there (a queued
// one, or else the standing attack) and drops back to 0 when the command lands or fizzles.
namespace corundum::test {

namespace rules = gameplay::rules;

enum class Side : std::uint8_t { Party, Hostile };

using rules::Outcome;

/// A combatant's integer state for one encounter, under keys the rules choose.
using CombatantState = std::flat_map<std::string, int, std::less<>>;

/// One combatant as the rules see it.
struct Combatant {
  std::optional<std::string> creature{}; ///< The creature id; nullopt for the protagonist.

  CombatantState state{};
};

/// A combatant's fixed numbers for one encounter, from CombatRules::join.
struct CombatantSpec {
  std::string name{};

  int start_beats{}; ///< Where the marker starts.
};

/// The timeline's constants and the callbacks that decide each combatant's numbers, commands and
/// results. Every callback must be set.
struct CombatRules {
  int command_point_beats{}; ///< Where a marker's command is carried out; must be positive.

  int reach_tiles{1}; ///< Tiles every standing attack reaches; must be positive.

  float seconds_per_tick{}; ///< Game time per timeline tick.

  int tempo_beats{}; ///< Beats a marker gains per tick; must be positive.

  int windup_beats{}; ///< Beats past the command point before every command lands.

  std::function<bool(const rules::CombatContext &, const Combatant &)> can_act{};

  /// The same commands in the same order for the whole encounter.
  std::function<std::vector<rules::CommandOption>(const rules::CombatContext &, const Combatant &)> commands{};

  std::function<rules::Health(const rules::CombatContext &, const Combatant &)> health{};

  std::function<std::expected<CombatantSpec, std::string>(const rules::CombatContext &, Combatant &)> join{};

  std::function<std::string(const rules::CombatContext &, Combatant &actor, Combatant &target)> resolve{};

  std::function<std::string(const rules::CombatContext &, std::string_view command, Combatant &actor)> resolve_self{};
};

enum class Phase : std::uint8_t {
  Ready,  ///< Filling toward the command point, or held there until its command can start.
  WindUp, ///< Past the command point; the command lands when its wind-up ends.
};

/** @brief One combatant joining an encounter. */
struct Joining {
  Combatant combatant{};

  Side side{Side::Party};
};

/** @brief What one Encounter step produced. */
struct EncounterStep {
  std::vector<std::string> reports{}; ///< One line per command that resolved, in order.

  std::vector<std::size_t> windups_started{}; ///< Combatants whose wind-up started this step, by index.
};

/** @brief One fight's timeline: markers, attack orders, queued commands and the outcome. */
class Encounter {
public:
  /** @brief Join every combatant through @p rules, list its commands, and put each marker at its
   *  start beats.
   *  @param steps_per_tick Fixed steps per timeline tick; at least 1.
   *  @return The encounter, or an error: a callback in @p rules is empty, its command point,
   *          tempo or reach isn't positive, or a combatant's join failed (the message names it). */
  [[nodiscard]] static std::expected<Encounter, std::string>
  start(CombatRules rules, const rules::CombatContext &context, std::vector<Joining> joining, int steps_per_tick);

  /** @brief Give @p attacker a standing attack on @p target, replacing any order. The order
   *  holds until the target can't act or a new order replaces it; a wind-up in progress goes
   *  on against the new target.
   *  @pre Both indices are below size() and on different sides. */
  void order_attack(std::size_t attacker, std::size_t target);

  /** @brief @p mover walks on its own: drop its attack order and cancel a wind-up, losing the
   *  command winding up; the marker drops to 0, as a fizzle's does. A queued command that hasn't
   *  started stays queued.
   *  @pre mover < size(). */
  void order_move(std::size_t mover);

  /** @brief Queue commands(actor)[@p option] as @p actor's next command, replacing any queued one.
   *  It starts at the next command point instead of the standing attack, which then resumes; it
   *  needs no target and no reach. A wind-up in progress is unaffected.
   *  @pre actor < size(), option < commands(actor).size(), and that option is available. */
  void queue_command(std::size_t actor, std::size_t option);

  /** @brief Drop @p actor's queued command, so the standing attack comes next.
   *  @pre actor < size(). */
  void clear_queued(std::size_t actor);

  /** @brief Advance one fixed step; every steps_per_tick-th step is a tick. Does nothing once
   *  outcome() is set.
   *  @param tiles Each combatant's tile, by index.
   *  @pre tiles.size() == size(). */
  [[nodiscard]] EncounterStep update(const rules::CombatContext &context, std::span<const rules::Tile> tiles);

  /** @brief How the fight ended, once one side has nobody who can act; nullopt until then. */
  [[nodiscard]] std::optional<Outcome> outcome() const noexcept { return outcome_; }

  [[nodiscard]] std::size_t size() const noexcept { return specs_.size(); }

  /** @brief The tiles a standing attack reaches; a diagonal step counts as one. */
  [[nodiscard]] int reach_tiles() const noexcept { return rules_.reach_tiles; }

  /** @pre index < size() for every per-combatant query below. */
  [[nodiscard]] const CombatantSpec &spec(std::size_t index) const { return specs_[index]; }

  [[nodiscard]] Side side(std::size_t index) const { return sides_[index]; }

  [[nodiscard]] Phase phase(std::size_t index) const { return markers_[index].phase; }

  [[nodiscard]] int beats(std::size_t index) const { return markers_[index].beats; }

  [[nodiscard]] std::optional<std::size_t> target_of(std::size_t index) const { return markers_[index].target; }

  /** @brief @p index's commands, as CombatRules::commands last listed them; refreshed every tick. */
  [[nodiscard]] std::span<const rules::CommandOption> commands(std::size_t index) const { return commands_[index]; }

  /** @brief @p index's queued command, as an index into commands(index); nullopt for none. */
  [[nodiscard]] std::optional<std::size_t> queued(std::size_t index) const { return markers_[index].queued; }

  /** @brief The seconds until @p index's command lands, as of now. A wind-up ends on the first tick
   *  its beats are reached, so this is a whole number of ticks. Battle calls it on the step a
   *  wind-up starts, when the marker may already be past the command point.
   *  @pre phase(index) == Phase::WindUp. */
  [[nodiscard]] float windup_seconds(std::size_t index) const;

  /** @brief Whether @p index is winding up its standing attack; false for a queued command or no
   *  wind-up. */
  [[nodiscard]] bool is_winding_up_at_enemy(std::size_t index) const;

  [[nodiscard]] rules::Health health(const rules::CombatContext &context, std::size_t index) const;

  [[nodiscard]] bool can_act(const rules::CombatContext &context, std::size_t index) const;

private:
  struct Marker {
    std::optional<rules::CommandOption> active{}; ///< The command winding up; nullopt for the standing attack.

    int beats{};

    Phase phase{Phase::Ready};

    std::optional<std::size_t> queued{};

    std::optional<std::size_t> target{};
  };

  Encounter(CombatRules rules, std::vector<Combatant> combatants, std::vector<Side> sides,
            std::vector<CombatantSpec> specs, std::vector<std::vector<rules::CommandOption>> commands,
            int steps_per_tick);

  CombatRules rules_;

  std::vector<Combatant> combatants_;

  std::vector<std::vector<rules::CommandOption>> commands_;

  std::vector<Side> sides_;

  std::vector<CombatantSpec> specs_;

  std::vector<Marker> markers_;

  int steps_per_tick_{1};

  int steps_into_tick_{};

  std::optional<Outcome> outcome_{};
};

/// The flat combat a test installs. `rules` is copied into each fight as it starts, so a test
/// changes it before start_encounter; `encounter` is the timeline of the last fight started.
struct FlatCombat {
  std::shared_ptr<Encounter> encounter{};

  CombatRules rules{};
};

/// A Ruleset::start_fight that runs each fight on an Encounter over @p combat's rules: the
/// protagonist on Side::Party, then one Side::Hostile combatant per creature, each with a
/// standing attack on the protagonist.
[[nodiscard]] rules::StartFight make_flat_start_fight(std::shared_ptr<FlatCombat> combat);

} // namespace corundum::test
