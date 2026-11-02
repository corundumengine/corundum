// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/gameplay/rules/rules.hpp>
#include <corundum/world/picking.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The combat timeline, independent of the scene. Every combatant's marker fills toward the command
// point, winds up a command there (a queued one-off, a hostile's chosen command, or else the
// standing attack) and drops back when the command lands or fizzles. A command's result can Break
// or Stagger its target's marker. Combatants are indices, positions are tiles and the numbers come
// from rules::CombatRules; combat::Battle maps an encounter onto the scene.
namespace corundum::gameplay::combat {

  /** @brief The standing attack's label, on the command bar and as the name of its wind-up. */
  inline constexpr std::string_view k_attack_label = "Attack";

  enum class Side : std::uint8_t { Party, Hostile };

  /** @brief How a fight ended, from the party's side. */
  enum class Outcome : std::uint8_t { Won, Lost };

  enum class Phase : std::uint8_t {
    Ready,  ///< Filling toward the command point, or held there until its command can start.
    WindUp, ///< Past the command point; the command lands when its wind-up ends.
  };

  /** @brief One combatant joining an encounter. */
  struct Joining {
    rules::Combatant combatant{};

    Side side{Side::Party};
  };

  /** @brief What one Encounter step produced. */
  struct EncounterStep {
    std::vector<std::string> reports{}; ///< One line per command that resolved and per Break, in order.

    std::vector<std::size_t> windups_started{}; ///< Combatants whose wind-up started this step, by index.
  };

  /** @brief One fight's timeline: markers, attack orders, queued commands and the outcome. */
  class Encounter {
  public:
    /** @brief Join every combatant through @p rules, list its commands, and put each marker at its
     *  start beats.
     *  @param steps_per_tick Fixed steps per timeline tick; at least 1.
     *  @return The encounter, or an error: a callback in @p rules is empty, its command point or
     *          Tempo isn't positive, or a combatant's join failed (the message names it). */
    [[nodiscard]] static std::expected<Encounter, std::string> start(rules::CombatRules rules,
                                                                     const rules::CombatContext &context,
                                                                     std::vector<Joining> joining, int steps_per_tick);

    /** @brief Give @p attacker a standing attack on @p target, replacing any order. The order
     *  holds until the target can't act or a new order replaces it; a wind-up in progress goes
     *  on against the new target.
     *  @pre Both indices are below size() and on different sides. */
    void order_attack(std::size_t attacker, std::size_t target);

    /** @brief @p mover walks on its own: drop its attack order and cancel a wind-up, losing the
     *  command winding up; the marker drops to that command's return point, as a fizzle's does. A
     *  queued command that hasn't started stays queued.
     *  @pre mover < size(). */
    void order_move(std::size_t mover);

    /** @brief Queue commands(actor)[@p option] as @p actor's next command, replacing any queued one.
     *  It winds up at the next command point instead of the standing attack, which then resumes. An
     *  Enemy command goes against the attack target and, like the attack, waits at the command point
     *  until that target is in reach (with no attack target, it holds there as the standing attack
     *  does, until order_attack gives one); a Self command starts there at once. A wind-up in
     *  progress is unaffected.
     *  @pre actor < size(), option < commands(actor).size(), and that option is available. */
    void queue_command(std::size_t actor, std::size_t option);

    /** @brief Drop @p actor's queued command, so the standing attack comes next.
     *  @pre actor < size(). */
    void clear_queued(std::size_t actor);

    /** @brief Advance one fixed step; every steps_per_tick-th step is a tick. Does nothing once
     *  outcome() is set.
     *  @param tiles Each combatant's tile, by index.
     *  @pre tiles.size() == size(). */
    [[nodiscard]] EncounterStep update(const rules::CombatContext &context, std::span<const world::TileCoord> tiles);

    /** @brief How the fight ended, once one side has nobody who can act; nullopt until then. */
    [[nodiscard]] std::optional<Outcome> outcome() const noexcept { return outcome_; }

    [[nodiscard]] std::size_t size() const noexcept { return specs_.size(); }

    /** @pre index < size() for every per-combatant query below. */
    [[nodiscard]] const rules::CombatantSpec &spec(std::size_t index) const { return specs_[index]; }

    [[nodiscard]] Side side(std::size_t index) const { return sides_[index]; }

    [[nodiscard]] Phase phase(std::size_t index) const { return markers_[index].phase; }

    [[nodiscard]] int beats(std::size_t index) const { return markers_[index].beats; }

    [[nodiscard]] std::optional<std::size_t> target_of(std::size_t index) const { return markers_[index].target; }

    /** @brief @p index's commands, as CombatRules::commands last listed them; refreshed after every
     *  command resolves. */
    [[nodiscard]] std::span<const rules::CommandOption> commands(std::size_t index) const { return commands_[index]; }

    /** @brief @p index's queued command, as an index into commands(index); nullopt for none. */
    [[nodiscard]] std::optional<std::size_t> queued(std::size_t index) const { return markers_[index].queued; }

    /** @brief The label of the command @p index is winding up (k_attack_label for the standing
     *  attack); empty when it isn't winding up. */
    [[nodiscard]] std::string_view windup_label(std::size_t index) const;

    /** @brief The beats at which @p index's command lands: the command point plus the wind-up of
     *  the command winding up, or else of the next one (its queued command, or else the standing
     *  attack; a hostile's choice isn't known until its wind-up starts). */
    [[nodiscard]] int windup_end_beats(std::size_t index) const;

    [[nodiscard]] rules::Health health(const rules::CombatContext &context, std::size_t index) const;

    [[nodiscard]] bool can_act(const rules::CombatContext &context, std::size_t index) const;

    [[nodiscard]] int command_point_beats() const noexcept { return rules_.command_point_beats; }

  private:
    struct Marker {
      std::optional<rules::CommandOption> active{}; ///< The command winding up; nullopt for the standing attack.

      int beats{};

      Phase phase{Phase::Ready};

      std::optional<std::size_t> queued{};

      std::optional<std::size_t> target{};
    };

    Encounter(rules::CombatRules rules, std::vector<rules::Combatant> combatants, std::vector<Side> sides,
              std::vector<rules::CombatantSpec> specs, std::vector<std::vector<rules::CommandOption>> commands,
              int steps_per_tick);

    rules::CombatRules rules_;

    std::vector<rules::Combatant> combatants_;

    std::vector<std::vector<rules::CommandOption>> commands_;

    std::vector<Side> sides_;

    std::vector<rules::CombatantSpec> specs_;

    std::vector<Marker> markers_;

    int steps_per_tick_{1};

    int steps_into_tick_{};

    std::optional<Outcome> outcome_{};
  };

} // namespace corundum::gameplay::combat
