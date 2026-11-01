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
#include <vector>

// The combat timeline, independent of the scene. Every combatant's marker fills toward the command
// point, winds up its standing attack once its target is in reach, and drops back when the attack
// lands or fizzles. Combatants are indices, positions are tiles and the numbers come from
// rules::CombatRules; combat::Battle maps an encounter onto the scene.
namespace corundum::gameplay::combat {

  enum class Side : std::uint8_t { Party, Hostile };

  /** @brief How a fight ended, from the party's side. */
  enum class Outcome : std::uint8_t { Won, Lost };

  enum class Phase : std::uint8_t {
    Ready,  ///< Filling toward the command point, or held there until the target is in reach.
    WindUp, ///< Past the command point; the standing attack lands when the wind-up ends.
  };

  /** @brief One combatant joining an encounter. */
  struct Joining {
    rules::Combatant combatant{};

    Side side{Side::Party};
  };

  /** @brief One fight's timeline: markers, standing attack orders and the outcome. */
  class Encounter {
  public:
    /** @brief Join every combatant through @p rules and put each marker at its start beats.
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

    /** @brief @p mover walks on its own: drop its order and cancel a wind-up, whose marker drops
     *  to the attack's return point as a fizzle's does.
     *  @pre mover < size(). */
    void order_move(std::size_t mover);

    /** @brief Advance one fixed step; every steps_per_tick-th step is a tick. Does nothing once
     *  outcome() is set.
     *  @param tiles Each combatant's tile, by index.
     *  @pre tiles.size() == size().
     *  @return One report per attack that landed this step, in the order they landed. */
    [[nodiscard]] std::vector<std::string> update(const rules::CombatContext &context,
                                                  std::span<const world::TileCoord> tiles);

    /** @brief How the fight ended, once one side has nobody who can act; nullopt until then. */
    [[nodiscard]] std::optional<Outcome> outcome() const noexcept { return outcome_; }

    [[nodiscard]] std::size_t size() const noexcept { return specs_.size(); }

    /** @pre index < size() for every per-combatant query below. */
    [[nodiscard]] const rules::CombatantSpec &spec(std::size_t index) const { return specs_[index]; }

    [[nodiscard]] Side side(std::size_t index) const { return sides_[index]; }

    [[nodiscard]] Phase phase(std::size_t index) const { return markers_[index].phase; }

    [[nodiscard]] int beats(std::size_t index) const { return markers_[index].beats; }

    [[nodiscard]] std::optional<std::size_t> target_of(std::size_t index) const { return markers_[index].target; }

    [[nodiscard]] rules::Health health(const rules::CombatContext &context, std::size_t index) const;

    [[nodiscard]] bool can_act(const rules::CombatContext &context, std::size_t index) const;

    [[nodiscard]] int command_point_beats() const noexcept { return rules_.command_point_beats; }

  private:
    struct Marker {
      int beats{};

      Phase phase{Phase::Ready};

      std::optional<std::size_t> target{};
    };

    Encounter(rules::CombatRules rules, std::vector<rules::Combatant> combatants, std::vector<Side> sides,
              std::vector<rules::CombatantSpec> specs, int steps_per_tick);

    rules::CombatRules rules_;

    std::vector<rules::Combatant> combatants_;

    std::vector<Side> sides_;

    std::vector<rules::CombatantSpec> specs_;

    std::vector<Marker> markers_;

    int steps_per_tick_{1};

    int steps_into_tick_{};

    std::optional<Outcome> outcome_{};
  };

} // namespace corundum::gameplay::combat
