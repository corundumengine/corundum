// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/gameplay/combat/encounter.hpp>

#include <corundum/gameplay/rules/rules.hpp>
#include <corundum/world/grid.hpp>
#include <corundum/world/picking.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <expected>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace corundum::gameplay::combat {

  namespace {

    /// One combatant whose wind-up ends on this tick, with the beats its marker reached.
    struct Landing {
      std::size_t index{};

      int beats{};
    };

    [[nodiscard]] bool callback_missing(const rules::CombatRules &rules, std::string &missing) {
      if (!rules.attack)
        missing = "attack";
      else if (!rules.can_act)
        missing = "can_act";
      else if (!rules.health)
        missing = "health";
      else if (!rules.join)
        missing = "join";
      return !missing.empty();
    }

    /// Whether @p index's ordered target stands within the attacker's reach.
    template <typename Marker>
    [[nodiscard]] bool in_reach(std::span<const world::TileCoord> tiles, const std::vector<rules::CombatantSpec> &specs,
                                const std::vector<Marker> &markers, std::size_t index) {
      const std::optional<std::size_t> target = markers[index].target;
      if (!target)
        return false;
      return world::tile_distance(tiles[index], tiles[*target]) <= specs[index].reach_tiles;
    }

    /// Advance one marker by its Tempo; returns true when a wind-up ends on this tick.
    template <typename Marker>
    [[nodiscard]] bool advance_marker(const rules::CombatRules &rules, const rules::CombatantSpec &spec, Marker &marker,
                                      bool target_in_reach) {
      if (marker.phase == Phase::WindUp) {
        marker.beats += rules.tempo_beats;
        return marker.beats >= rules.command_point_beats + spec.attack.windup_beats;
      }
      // Held at the command point: start the wind-up once the target is in reach, without moving.
      if (marker.beats >= rules.command_point_beats) {
        if (target_in_reach)
          marker.phase = Phase::WindUp;
        return false;
      }
      marker.beats += rules.tempo_beats;
      if (marker.beats < rules.command_point_beats)
        return false;
      if (target_in_reach) {
        marker.phase = Phase::WindUp;
        return false;
      }
      marker.beats = rules.command_point_beats;
      return false;
    }

    /// Advance every marker that can act, collecting the wind-ups that end on this tick.
    template <typename Marker>
    void advance_tick(const rules::CombatRules &rules, const rules::CombatContext &context,
                      const std::vector<rules::Combatant> &combatants, const std::vector<rules::CombatantSpec> &specs,
                      std::span<const world::TileCoord> tiles, std::vector<Marker> &markers,
                      std::vector<Landing> &landing) {
      for (std::size_t index = 0; index < markers.size(); ++index) {
        if (!rules.can_act(context, combatants[index]))
          continue;
        const bool target_in_reach = in_reach(tiles, specs, markers, index);
        if (advance_marker(rules, specs[index], markers[index], target_in_reach))
          landing.push_back({.index = index, .beats = markers[index].beats});
      }
    }

    /// Order the tick's landings: further along, then higher initiative, then the party, then join
    /// order (which stable_sort keeps for the remaining ties).
    void sort_landings(std::vector<Landing> &landing, const std::vector<rules::CombatantSpec> &specs,
                       const std::vector<Side> &sides) {
      std::ranges::stable_sort(landing, [&](const Landing &first, const Landing &second) {
        if (first.beats != second.beats)
          return first.beats > second.beats;
        const int first_initiative = specs[first.index].initiative;
        const int second_initiative = specs[second.index].initiative;
        if (first_initiative != second_initiative)
          return first_initiative > second_initiative;
        return sides[first.index] == Side::Party && sides[second.index] == Side::Hostile;
      });
    }

    /// Carry out the tick's landings in order: drop each marker to its return point, then attack,
    /// fizzle when the target left reach, or drop the order when the target is down.
    template <typename Marker>
    void land_attacks(const rules::CombatRules &rules, const rules::CombatContext &context,
                      std::vector<rules::Combatant> &combatants, const std::vector<rules::CombatantSpec> &specs,
                      std::span<const world::TileCoord> tiles, std::vector<Marker> &markers,
                      const std::vector<Landing> &landing, std::vector<std::string> &reports) {
      for (const Landing &entry : landing) {
        const std::size_t index = entry.index;
        if (!rules.can_act(context, combatants[index]))
          continue;
        Marker &marker = markers[index];
        marker.phase = Phase::Ready;
        marker.beats = specs[index].attack.return_to_beats;
        if (!marker.target || !rules.can_act(context, combatants[*marker.target])) {
          marker.target.reset();
          continue;
        }
        if (in_reach(tiles, specs, markers, index))
          reports.push_back(rules.attack(context, combatants[index], combatants[*marker.target]));
      }
    }

    /// Drop every order whose target can no longer act.
    template <typename Marker>
    void drop_dead_orders(const rules::CombatRules &rules, const rules::CombatContext &context,
                          const std::vector<rules::Combatant> &combatants, std::vector<Marker> &markers) {
      for (Marker &marker : markers) {
        if (marker.target && !rules.can_act(context, combatants[*marker.target]))
          marker.target.reset();
      }
    }

    [[nodiscard]] bool side_can_act(const rules::CombatRules &rules, const rules::CombatContext &context,
                                    const std::vector<rules::Combatant> &combatants, const std::vector<Side> &sides,
                                    Side side) {
      for (std::size_t index = 0; index < combatants.size(); ++index) {
        if (sides[index] == side && rules.can_act(context, combatants[index]))
          return true;
      }
      return false;
    }

  } // namespace

  std::expected<Encounter, std::string> Encounter::start(rules::CombatRules rules, const rules::CombatContext &context,
                                                         std::vector<Joining> joining, int steps_per_tick) {
    assert(steps_per_tick >= 1);

    std::string missing;
    if (callback_missing(rules, missing))
      return std::unexpected(std::format("[gameplay] the combat rules have no '{}' callback", missing));
    if (rules.command_point_beats <= 0 || rules.tempo_beats <= 0)
      return std::unexpected("[gameplay] the combat rules need a positive command_point_beats and tempo_beats");

    std::vector<rules::Combatant> combatants;
    std::vector<Side> sides;
    std::vector<rules::CombatantSpec> specs;
    combatants.reserve(joining.size());
    sides.reserve(joining.size());
    specs.reserve(joining.size());
    for (Joining &entry : joining) {
      std::expected<rules::CombatantSpec, std::string> spec = rules.join(context, entry.combatant);
      if (!spec) {
        const std::string who =
            entry.combatant.creature ? std::format("'{}'", *entry.combatant.creature) : std::string{"the protagonist"};
        return std::unexpected(std::format("[gameplay] {} can't join the encounter: {}", who, spec.error()));
      }
      combatants.push_back(std::move(entry.combatant));
      sides.push_back(entry.side);
      specs.push_back(std::move(*spec));
    }

    return Encounter(std::move(rules), std::move(combatants), std::move(sides), std::move(specs), steps_per_tick);
  }

  void Encounter::order_attack(std::size_t attacker, std::size_t target) {
    assert(attacker < size());
    assert(target < size());
    assert(sides_[attacker] != sides_[target]);

    markers_[attacker].target = target;
  }

  void Encounter::order_move(std::size_t mover) {
    assert(mover < size());

    Marker &marker = markers_[mover];
    marker.target.reset();
    if (marker.phase == Phase::WindUp) {
      marker.phase = Phase::Ready;
      marker.beats = specs_[mover].attack.return_to_beats;
    }
  }

  std::vector<std::string> Encounter::update(const rules::CombatContext &context,
                                             std::span<const world::TileCoord> tiles) {
    assert(tiles.size() == size());
    std::vector<std::string> reports;
    if (outcome_)
      return reports;

    ++steps_into_tick_;
    if (steps_into_tick_ >= steps_per_tick_) {
      steps_into_tick_ = 0;
      std::vector<Landing> landing;
      advance_tick(rules_, context, combatants_, specs_, tiles, markers_, landing);
      sort_landings(landing, specs_, sides_);
      land_attacks(rules_, context, combatants_, specs_, tiles, markers_, landing, reports);
    }

    drop_dead_orders(rules_, context, combatants_, markers_);

    if (!side_can_act(rules_, context, combatants_, sides_, Side::Party))
      outcome_ = Outcome::Lost;
    else if (!side_can_act(rules_, context, combatants_, sides_, Side::Hostile))
      outcome_ = Outcome::Won;

    return reports;
  }

  rules::Health Encounter::health(const rules::CombatContext &context, std::size_t index) const {
    assert(index < size());
    return rules_.health(context, combatants_[index]);
  }

  bool Encounter::can_act(const rules::CombatContext &context, std::size_t index) const {
    assert(index < size());
    return rules_.can_act(context, combatants_[index]);
  }

  Encounter::Encounter(rules::CombatRules rules, std::vector<rules::Combatant> combatants, std::vector<Side> sides,
                       std::vector<rules::CombatantSpec> specs, int steps_per_tick)
      : rules_(std::move(rules)), combatants_(std::move(combatants)), sides_(std::move(sides)),
        specs_(std::move(specs)), steps_per_tick_(steps_per_tick) {
    markers_.reserve(specs_.size());
    for (const rules::CombatantSpec &spec : specs_)
      markers_.push_back({.beats = spec.start_beats, .phase = Phase::Ready, .target = std::nullopt});
  }

} // namespace corundum::gameplay::combat
