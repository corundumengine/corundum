// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/gameplay/combat/encounter.hpp>

#include <corundum/core/verify.hpp>
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
#include <string_view>
#include <utility>
#include <vector>

namespace corundum::gameplay::combat {

  namespace {

    /// One combatant whose wind-up ends on this tick, with the beats its marker reached.
    struct Landing {
      std::size_t index{};

      int beats{};
    };

    /// The name of the first empty callback in @p rules, in declaration order; nullopt when all are set.
    [[nodiscard]] std::optional<std::string_view> first_missing_callback(const rules::CombatRules &rules) {
      if (!rules.can_act)
        return "can_act";
      if (!rules.commands)
        return "commands";
      if (!rules.health)
        return "health";
      if (!rules.hostile_command)
        return "hostile_command";
      if (!rules.join)
        return "join";
      if (!rules.resolve)
        return "resolve";
      if (!rules.resolve_self)
        return "resolve_self";
      return std::nullopt;
    }

    /// The option a Ready marker will start next: a party combatant's queued command, or nullopt
    /// for the standing attack (and for a hostile, whose choice isn't known until its wind-up
    /// starts).
    template <typename Marker>
    [[nodiscard]] const rules::CommandOption *
    next_option(const Marker &marker, const std::vector<std::vector<rules::CommandOption>> &commands,
                const std::vector<Side> &sides, std::size_t index) {
      if (sides[index] != Side::Party || !marker.queued)
        return nullptr;
      return &commands[index][*marker.queued];
    }

    /// The timing of the command a Ready marker will start: its queued option, or else the standing
    /// attack.
    template <typename Marker>
    [[nodiscard]] const rules::Command &next_timing(const Marker &marker, const rules::CombatantSpec &spec,
                                                    const std::vector<std::vector<rules::CommandOption>> &commands,
                                                    const std::vector<Side> &sides, std::size_t index) {
      const rules::CommandOption *option = next_option(marker, commands, sides, index);
      return option != nullptr ? option->timing : spec.attack;
    }

    /// The timing of the command a marker is winding up: its active option, or else the standing
    /// attack.
    template <typename Marker>
    [[nodiscard]] const rules::Command &winding_timing(const Marker &marker, const rules::CombatantSpec &spec) {
      return marker.active ? marker.active->timing : spec.attack;
    }

    /// The label of the command a marker is winding up.
    template <typename Marker> [[nodiscard]] std::string_view winding_label(const Marker &marker) {
      return marker.active ? std::string_view{marker.active->label} : k_attack_label;
    }

    /// Whether @p index's ordered target stands within its reach.
    template <typename Marker>
    [[nodiscard]] bool in_reach(std::span<const world::TileCoord> tiles, const std::vector<rules::CombatantSpec> &specs,
                                const std::vector<Marker> &markers, std::size_t index) {
      const std::optional<std::size_t> target = markers[index].target;
      if (!target)
        return false;
      return world::tile_distance(tiles[index], tiles[*target]) <= specs[index].reach_tiles;
    }

    /// Whether @p index's next command can start: a Self command needs nothing, an Enemy command
    /// needs its target in reach.
    template <typename Marker>
    [[nodiscard]] bool can_start_command(const Marker &marker,
                                         const std::vector<std::vector<rules::CommandOption>> &commands,
                                         const std::vector<Side> &sides, std::span<const world::TileCoord> tiles,
                                         const std::vector<rules::CombatantSpec> &specs,
                                         const std::vector<Marker> &markers, std::size_t index) {
      const rules::CommandOption *next = next_option(marker, commands, sides, index);
      if (next != nullptr && next->target == rules::CommandTarget::Self)
        return true;
      return in_reach(tiles, specs, markers, index);
    }

    /// Start @p index's wind-up: a hostile chooses its command now, a party combatant starts its
    /// queued one (or the standing attack) and loses its queue.
    template <typename Marker>
    void start_windup(const rules::CombatRules &rules, const rules::CombatContext &context,
                      std::vector<rules::Combatant> &combatants,
                      const std::vector<std::vector<rules::CommandOption>> &commands, const std::vector<Side> &sides,
                      std::vector<Marker> &markers, std::size_t index, std::vector<std::size_t> &windups_started) {
      Marker &marker = markers[index];
      marker.phase = Phase::WindUp;
      if (sides[index] == Side::Hostile) {
        marker.active = rules.hostile_command(context, combatants[index]);
      } else {
        marker.active =
            marker.queued ? std::optional<rules::CommandOption>{commands[index][*marker.queued]} : std::nullopt;
        marker.queued.reset();
      }
      windups_started.push_back(index);
    }

    /// Advance one marker by its Tempo; returns true when a wind-up ends on this tick.
    template <typename Marker>
    [[nodiscard]] bool advance_marker(const rules::CombatRules &rules, const rules::CombatContext &context,
                                      std::vector<rules::Combatant> &combatants, const rules::CombatantSpec &spec,
                                      const std::vector<std::vector<rules::CommandOption>> &commands,
                                      const std::vector<Side> &sides, std::span<const world::TileCoord> tiles,
                                      const std::vector<rules::CombatantSpec> &specs, std::vector<Marker> &markers,
                                      std::size_t index, std::vector<std::size_t> &windups_started) {
      Marker &marker = markers[index];
      if (marker.phase == Phase::WindUp) {
        marker.beats += rules.tempo_beats;
        return marker.beats >= rules.command_point_beats + winding_timing(marker, spec).windup_beats;
      }
      const bool can_start = can_start_command(marker, commands, sides, tiles, specs, markers, index);
      // Held at the command point: start the wind-up once the command can start, without moving.
      if (marker.beats >= rules.command_point_beats) {
        if (can_start)
          start_windup(rules, context, combatants, commands, sides, markers, index, windups_started);
        return false;
      }
      marker.beats += rules.tempo_beats;
      if (marker.beats < rules.command_point_beats)
        return false;
      if (can_start) {
        start_windup(rules, context, combatants, commands, sides, markers, index, windups_started);
        return false;
      }
      marker.beats = rules.command_point_beats;
      return false;
    }

    /// Advance every marker that can act, collecting the wind-ups that end on this tick.
    template <typename Marker>
    void advance_tick(const rules::CombatRules &rules, const rules::CombatContext &context,
                      std::vector<rules::Combatant> &combatants, const std::vector<rules::CombatantSpec> &specs,
                      const std::vector<std::vector<rules::CommandOption>> &commands, const std::vector<Side> &sides,
                      std::span<const world::TileCoord> tiles, std::vector<Marker> &markers,
                      std::vector<Landing> &landing, std::vector<std::size_t> &windups_started) {
      for (std::size_t index = 0; index < markers.size(); ++index) {
        if (!rules.can_act(context, combatants[index]))
          continue;
        if (advance_marker(rules, context, combatants, specs[index], commands, sides, tiles, specs, markers, index,
                           windups_started))
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

    /// Ask the rules for every combatant's commands again. The rules promise the same list in the
    /// same order; a queued index and the command bar index into it, so a change is fatal.
    void refresh_commands(const rules::CombatRules &rules, const rules::CombatContext &context,
                          const std::vector<rules::Combatant> &combatants,
                          std::vector<std::vector<rules::CommandOption>> &commands) {
      for (std::size_t index = 0; index < commands.size(); ++index) {
        std::vector<rules::CommandOption> listed = rules.commands(context, combatants[index]);
        core::verify(listed.size() == commands[index].size(),
                     "[gameplay] a combat ruleset changed the length of a command list mid-fight");
        commands[index] = std::move(listed);
      }
    }

    /// Apply a command's result to its target: Break first, then Stagger.
    template <typename Marker>
    void apply_result(const rules::CombatRules &rules, const std::vector<rules::CombatantSpec> &specs,
                      std::vector<Marker> &markers, std::size_t target, const rules::CommandResult &result,
                      std::vector<std::string> &reports) {
      Marker &marker = markers[target];
      if (result.breaks && marker.phase == Phase::WindUp) {
        const std::string_view label = winding_label(marker);
        reports.push_back(std::format("{}: {} broken", specs[target].name, label));
        marker.phase = Phase::Ready;
        marker.beats = rules.break_return_beats;
        marker.active.reset();
      }
      if (result.stagger_beats > 0)
        marker.beats = std::max(0, marker.beats - result.stagger_beats);
    }

    /// Take the command a landing marker was winding up, dropping the marker to that command's
    /// return point; nullopt for the standing attack.
    template <typename Marker>
    [[nodiscard]] std::optional<rules::CommandOption> take_command(Marker &marker, const rules::CombatantSpec &spec) {
      std::optional<rules::CommandOption> command = marker.active;
      marker.phase = Phase::Ready;
      marker.beats = command ? command->timing.return_to_beats : spec.attack.return_to_beats;
      marker.active.reset();
      return command;
    }

    /// Resolve a landed Enemy command against the marker's target, or fizzle it when the target
    /// left reach or can no longer act.
    template <typename Marker>
    void land_enemy(const rules::CombatRules &rules, const rules::CombatContext &context,
                    std::vector<rules::Combatant> &combatants, const std::vector<rules::CombatantSpec> &specs,
                    std::vector<std::vector<rules::CommandOption>> &commands, std::span<const world::TileCoord> tiles,
                    std::vector<Marker> &markers, std::size_t index, const std::optional<rules::CommandOption> &command,
                    std::vector<std::string> &reports) {
      Marker &marker = markers[index];
      if (!marker.target || !rules.can_act(context, combatants[*marker.target])) {
        marker.target.reset();
        return;
      }
      if (!in_reach(tiles, specs, markers, index))
        return;
      const rules::CommandResult result = rules.resolve(context, command ? command->id : std::string_view{},
                                                        combatants[index], combatants[*marker.target]);
      reports.push_back(result.report);
      apply_result(rules, specs, markers, *marker.target, result, reports);
      refresh_commands(rules, context, combatants, commands);
    }

    /// Carry out one landed command: a Self command on its actor, an Enemy command on its target.
    template <typename Marker>
    void land_command(const rules::CombatRules &rules, const rules::CombatContext &context,
                      std::vector<rules::Combatant> &combatants, const std::vector<rules::CombatantSpec> &specs,
                      std::vector<std::vector<rules::CommandOption>> &commands, std::span<const world::TileCoord> tiles,
                      std::vector<Marker> &markers, std::size_t index, std::vector<std::string> &reports) {
      const std::optional<rules::CommandOption> command = take_command(markers[index], specs[index]);
      if (command && command->target == rules::CommandTarget::Self) {
        reports.push_back(rules.resolve_self(context, command->id, combatants[index]));
        refresh_commands(rules, context, combatants, commands);
        return;
      }
      land_enemy(rules, context, combatants, specs, commands, tiles, markers, index, command, reports);
    }

    /// Carry out the tick's landings in order, skipping combatants downed, Broken or Staggered off
    /// their landing point earlier in the same tick.
    template <typename Marker>
    void land_commands(const rules::CombatRules &rules, const rules::CombatContext &context,
                       std::vector<rules::Combatant> &combatants, const std::vector<rules::CombatantSpec> &specs,
                       std::vector<std::vector<rules::CommandOption>> &commands,
                       std::span<const world::TileCoord> tiles, std::vector<Marker> &markers,
                       const std::vector<Landing> &landing, std::vector<std::string> &reports) {
      for (const Landing &entry : landing) {
        const std::size_t index = entry.index;
        const Marker &marker = markers[index];
        if (!rules.can_act(context, combatants[index]) || marker.phase != Phase::WindUp)
          continue;
        const int end = rules.command_point_beats + winding_timing(marker, specs[index]).windup_beats;
        if (marker.beats < end)
          continue;
        land_command(rules, context, combatants, specs, commands, tiles, markers, index, reports);
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

    if (const std::optional<std::string_view> missing = first_missing_callback(rules))
      return std::unexpected(std::format("[gameplay] the combat rules have no '{}' callback", *missing));
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

    std::vector<std::vector<rules::CommandOption>> commands;
    commands.reserve(combatants.size());
    for (const rules::Combatant &combatant : combatants)
      commands.push_back(rules.commands(context, combatant));

    return Encounter(std::move(rules), std::move(combatants), std::move(sides), std::move(specs), std::move(commands),
                     steps_per_tick);
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
      marker.beats = winding_timing(marker, specs_[mover]).return_to_beats;
      marker.active.reset();
    }
  }

  void Encounter::queue_command(std::size_t actor, std::size_t option) {
    assert(actor < size());
    assert(option < commands_[actor].size());
    assert(commands_[actor][option].is_available);

    markers_[actor].queued = option;
  }

  void Encounter::clear_queued(std::size_t actor) {
    assert(actor < size());

    markers_[actor].queued.reset();
  }

  EncounterStep Encounter::update(const rules::CombatContext &context, std::span<const world::TileCoord> tiles) {
    assert(tiles.size() == size());
    EncounterStep step;
    if (outcome_)
      return step;

    ++steps_into_tick_;
    if (steps_into_tick_ >= steps_per_tick_) {
      steps_into_tick_ = 0;
      std::vector<Landing> landing;
      advance_tick(rules_, context, combatants_, specs_, commands_, sides_, tiles, markers_, landing,
                   step.windups_started);
      sort_landings(landing, specs_, sides_);
      land_commands(rules_, context, combatants_, specs_, commands_, tiles, markers_, landing, step.reports);
    }

    drop_dead_orders(rules_, context, combatants_, markers_);

    if (!side_can_act(rules_, context, combatants_, sides_, Side::Party))
      outcome_ = Outcome::Lost;
    else if (!side_can_act(rules_, context, combatants_, sides_, Side::Hostile))
      outcome_ = Outcome::Won;

    return step;
  }

  std::string_view Encounter::windup_label(std::size_t index) const {
    assert(index < size());
    const Marker &marker = markers_[index];
    if (marker.phase != Phase::WindUp)
      return {};
    return winding_label(marker);
  }

  int Encounter::windup_end_beats(std::size_t index) const {
    assert(index < size());
    const Marker &marker = markers_[index];
    const rules::Command &command = marker.phase == Phase::WindUp
                                        ? winding_timing(marker, specs_[index])
                                        : next_timing(marker, specs_[index], commands_, sides_, index);
    return rules_.command_point_beats + command.windup_beats;
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
                       std::vector<rules::CombatantSpec> specs, std::vector<std::vector<rules::CommandOption>> commands,
                       int steps_per_tick)
      : rules_(std::move(rules)), combatants_(std::move(combatants)), commands_(std::move(commands)),
        sides_(std::move(sides)), specs_(std::move(specs)), steps_per_tick_(steps_per_tick) {
    markers_.reserve(specs_.size());
    for (const rules::CombatantSpec &spec : specs_)
      markers_.push_back({
          .active = std::nullopt,
          .beats = spec.start_beats,
          .phase = Phase::Ready,
          .queued = std::nullopt,
          .target = std::nullopt,
      });
  }

} // namespace corundum::gameplay::combat
