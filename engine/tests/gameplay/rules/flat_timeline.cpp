// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include "gameplay/rules/flat_timeline.hpp"

#include <corundum/core/verify.hpp>
#include <corundum/gameplay/rules/rules.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace corundum::test {

namespace {

/// One combatant whose wind-up ends on this tick, with the beats its marker reached.
struct Landing {
  std::size_t index{};

  int beats{};
};

/// @p name when @p callback is empty, and nullopt when it is set.
template <typename Callback>
[[nodiscard]] std::optional<std::string_view> missing_callback(std::string_view name, const Callback &callback) {
  if (callback)
    return std::nullopt;
  return name;
}

/// The name of the first empty callback in @p rules, in declaration order; nullopt when all are set.
[[nodiscard]] std::optional<std::string_view> first_missing_callback(const CombatRules &rules) {
  const std::array checks{
      missing_callback("can_act", rules.can_act), missing_callback("commands", rules.commands),
      missing_callback("health", rules.health),   missing_callback("join", rules.join),
      missing_callback("resolve", rules.resolve), missing_callback("resolve_self", rules.resolve_self),
  };
  for (const std::optional<std::string_view> &check : checks) {
    if (check)
      return check;
  }
  return std::nullopt;
}

/// Whether @p index's ordered target stands within @p reach_tiles of it.
template <typename Marker>
[[nodiscard]] bool in_reach(std::span<const rules::Tile> tiles, int reach_tiles, const std::vector<Marker> &markers,
                            std::size_t index) {
  const std::optional<std::size_t> target = markers[index].target;
  if (!target)
    return false;
  return rules::tile_distance(tiles[index], tiles[*target]) <= reach_tiles;
}

/// Whether @p index's next command can start: a queued command needs nothing, the standing
/// attack needs its target in reach.
template <typename Marker>
[[nodiscard]] bool can_start_command(const CombatRules &rules, std::span<const rules::Tile> tiles,
                                     const std::vector<Marker> &markers, std::size_t index) {
  if (markers[index].queued)
    return true;
  return in_reach(tiles, rules.reach_tiles, markers, index);
}

/// Start @p marker's wind-up: its queued command, or else the standing attack. The queue empties.
template <typename Marker>
void start_windup(const std::vector<rules::CommandOption> &commands, Marker &marker, std::size_t index,
                  std::vector<std::size_t> &windups_started) {
  marker.phase = Phase::WindUp;
  marker.active = marker.queued ? std::optional<rules::CommandOption>{commands[*marker.queued]} : std::nullopt;
  marker.queued.reset();
  windups_started.push_back(index);
}

/// Advance one marker by CombatRules::tempo_beats; returns true when its wind-up ends on this
/// tick. A Ready marker that reaches the command point starts its wind-up once its command can
/// start, and is held there until then.
template <typename Marker>
[[nodiscard]] bool advance_marker(const CombatRules &rules,
                                  const std::vector<std::vector<rules::CommandOption>> &commands,
                                  std::span<const rules::Tile> tiles, std::vector<Marker> &markers, std::size_t index,
                                  std::vector<std::size_t> &windups_started) {
  Marker &marker = markers[index];
  if (marker.phase == Phase::WindUp) {
    marker.beats += rules.tempo_beats;
    return marker.beats >= rules.command_point_beats + rules.windup_beats;
  }
  const bool already_at_command_point = marker.beats >= rules.command_point_beats;
  if (!already_at_command_point) {
    marker.beats += rules.tempo_beats;
    if (marker.beats < rules.command_point_beats)
      return false;
  }

  // Start the wind-up once the command can start; one that only arrived this tick and can't
  // start yet is held at the command point instead.
  if (can_start_command(rules, tiles, markers, index))
    start_windup(commands[index], marker, index, windups_started);
  else if (!already_at_command_point)
    marker.beats = rules.command_point_beats;
  return false;
}

/// Advance every marker that can act, collecting the wind-ups that end on this tick.
template <typename Marker>
void advance_tick(const CombatRules &rules, const rules::CombatContext &context,
                  const std::vector<Combatant> &combatants,
                  const std::vector<std::vector<rules::CommandOption>> &commands, std::span<const rules::Tile> tiles,
                  std::vector<Marker> &markers, EncounterStep &step, std::vector<Landing> &landing) {
  for (std::size_t index = 0; index < markers.size(); ++index) {
    if (!rules.can_act(context, combatants[index]))
      continue;
    if (advance_marker(rules, commands, tiles, markers, index, step.windups_started))
      landing.push_back({.index = index, .beats = markers[index].beats});
  }
}

/// Order the tick's landings: further along, then the party, then join order (which stable_sort
/// keeps for the remaining ties).
void sort_landings(std::vector<Landing> &landing, const std::vector<Side> &sides) {
  std::ranges::stable_sort(landing, [&](const Landing &first, const Landing &second) {
    if (first.beats != second.beats)
      return first.beats > second.beats;
    return sides[first.index] == Side::Party && sides[second.index] == Side::Hostile;
  });
}

/// Ask the rules for every combatant's commands again, keeping each list's length: a queued
/// index and the command bar index into the list, so a change is fatal.
void refresh_commands(const CombatRules &rules, const rules::CombatContext &context,
                      const std::vector<Combatant> &combatants,
                      std::vector<std::vector<rules::CommandOption>> &commands) {
  for (std::size_t index = 0; index < commands.size(); ++index) {
    std::vector<rules::CommandOption> listed = rules.commands(context, combatants[index]);
    core::verify(listed.size() == commands[index].size(),
                 "a combat ruleset changed the length of a command list mid-fight");
    commands[index] = std::move(listed);
  }
}

/// Take the command a landing marker was winding up, dropping the marker to 0; nullopt for the
/// standing attack.
template <typename Marker> [[nodiscard]] std::optional<rules::CommandOption> take_command(Marker &marker) {
  std::optional<rules::CommandOption> command = marker.active;
  marker.phase = Phase::Ready;
  marker.beats = 0;
  marker.active.reset();
  return command;
}

/// Carry out a landed standing attack on the marker's target, or let it fizzle when the target
/// left reach or can no longer act.
template <typename Marker>
void land_attack(const CombatRules &rules, const rules::CombatContext &context, std::vector<Combatant> &combatants,
                 std::span<const rules::Tile> tiles, std::vector<Marker> &markers, std::size_t index,
                 std::vector<std::string> &reports) {
  Marker &marker = markers[index];
  const bool is_target_gone = !marker.target || !rules.can_act(context, combatants[*marker.target]);
  if (is_target_gone)
    marker.target.reset();
  if (is_target_gone || !in_reach(tiles, rules.reach_tiles, markers, index))
    return;
  reports.push_back(rules.resolve(context, combatants[index], combatants[*marker.target]));
}

/// Carry out one landed command: a queued command on its actor, the standing attack on its target.
template <typename Marker>
void land_command(const CombatRules &rules, const rules::CombatContext &context, std::vector<Combatant> &combatants,
                  std::span<const rules::Tile> tiles, std::vector<Marker> &markers, std::size_t index,
                  std::vector<std::string> &reports) {
  const std::optional<rules::CommandOption> command = take_command(markers[index]);
  if (command) {
    reports.push_back(rules.resolve_self(context, command->id, combatants[index]));
    return;
  }
  land_attack(rules, context, combatants, tiles, markers, index, reports);
}

/// Carry out the tick's landings in order, skipping combatants downed earlier in the same tick.
template <typename Marker>
void land_commands(const CombatRules &rules, const rules::CombatContext &context, std::vector<Combatant> &combatants,
                   std::span<const rules::Tile> tiles, std::vector<Marker> &markers,
                   const std::vector<Landing> &landing, std::vector<std::string> &reports) {
  for (const Landing &entry : landing) {
    if (!rules.can_act(context, combatants[entry.index]))
      continue;
    land_command(rules, context, combatants, tiles, markers, entry.index, reports);
  }
}

/// Drop every order whose target can no longer act.
template <typename Marker>
void drop_dead_orders(const CombatRules &rules, const rules::CombatContext &context,
                      const std::vector<Combatant> &combatants, std::vector<Marker> &markers) {
  for (Marker &marker : markers) {
    if (marker.target && !rules.can_act(context, combatants[*marker.target]))
      marker.target.reset();
  }
}

[[nodiscard]] bool side_can_act(const CombatRules &rules, const rules::CombatContext &context,
                                const std::vector<Combatant> &combatants, const std::vector<Side> &sides, Side side) {
  for (std::size_t index = 0; index < combatants.size(); ++index) {
    if (sides[index] == side && rules.can_act(context, combatants[index]))
      return true;
  }
  return false;
}

} // namespace

std::expected<Encounter, std::string> Encounter::start(CombatRules rules, const rules::CombatContext &context,
                                                       std::vector<Joining> joining, int steps_per_tick) {
  assert(steps_per_tick >= 1);

  if (const std::optional<std::string_view> missing = first_missing_callback(rules))
    return std::unexpected(std::format("the combat rules have no '{}' callback", *missing));
  const bool is_timeline_valid = rules.command_point_beats > 0 && rules.tempo_beats > 0;
  if (!is_timeline_valid || rules.reach_tiles <= 0)
    return std::unexpected("the combat rules need a positive command_point_beats, tempo_beats and reach_tiles");

  std::vector<Combatant> combatants;
  std::vector<Side> sides;
  std::vector<CombatantSpec> specs;
  combatants.reserve(joining.size());
  sides.reserve(joining.size());
  specs.reserve(joining.size());
  for (Joining &entry : joining) {
    std::expected<CombatantSpec, std::string> spec = rules.join(context, entry.combatant);
    if (!spec) {
      const std::string who =
          entry.combatant.creature ? std::format("'{}'", *entry.combatant.creature) : std::string{"the protagonist"};
      return std::unexpected(std::format("{} can't join the encounter: {}", who, spec.error()));
    }
    combatants.push_back(std::move(entry.combatant));
    sides.push_back(entry.side);
    specs.push_back(std::move(*spec));
  }

  std::vector<std::vector<rules::CommandOption>> commands;
  commands.reserve(combatants.size());
  for (const Combatant &combatant : combatants)
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
  if (marker.phase != Phase::WindUp)
    return;
  marker.phase = Phase::Ready;
  marker.beats = 0;
  marker.active.reset();
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

EncounterStep Encounter::update(const rules::CombatContext &context, std::span<const rules::Tile> tiles) {
  assert(tiles.size() == size());
  EncounterStep step;
  if (outcome_)
    return step;

  ++steps_into_tick_;
  if (steps_into_tick_ >= steps_per_tick_) {
    steps_into_tick_ = 0;
    std::vector<Landing> landing;
    advance_tick(rules_, context, combatants_, commands_, tiles, markers_, step, landing);
    sort_landings(landing, sides_);
    land_commands(rules_, context, combatants_, tiles, markers_, landing, step.reports);
    refresh_commands(rules_, context, combatants_, commands_);
  }

  drop_dead_orders(rules_, context, combatants_, markers_);

  if (!side_can_act(rules_, context, combatants_, sides_, Side::Party))
    outcome_ = Outcome::Lost;
  else if (!side_can_act(rules_, context, combatants_, sides_, Side::Hostile))
    outcome_ = Outcome::Won;

  return step;
}

float Encounter::windup_seconds(std::size_t index) const {
  assert(index < size());
  assert(markers_[index].phase == Phase::WindUp);
  const int windup_end = rules_.command_point_beats + rules_.windup_beats;
  const int beats_left = windup_end - markers_[index].beats;
  const int ticks = std::max(1, (beats_left + rules_.tempo_beats - 1) / rules_.tempo_beats);
  return static_cast<float>(ticks) * rules_.seconds_per_tick;
}

bool Encounter::is_winding_up_at_enemy(std::size_t index) const {
  assert(index < size());
  const Marker &marker = markers_[index];
  return marker.phase == Phase::WindUp && !marker.active;
}

rules::Health Encounter::health(const rules::CombatContext &context, std::size_t index) const {
  assert(index < size());
  return rules_.health(context, combatants_[index]);
}

bool Encounter::can_act(const rules::CombatContext &context, std::size_t index) const {
  assert(index < size());
  return rules_.can_act(context, combatants_[index]);
}

Encounter::Encounter(CombatRules rules, std::vector<Combatant> combatants, std::vector<Side> sides,
                     std::vector<CombatantSpec> specs, std::vector<std::vector<rules::CommandOption>> commands,
                     int steps_per_tick)
    : rules_(std::move(rules)), combatants_(std::move(combatants)), commands_(std::move(commands)),
      sides_(std::move(sides)), specs_(std::move(specs)), steps_per_tick_(steps_per_tick) {
  markers_.reserve(specs_.size());
  for (const CombatantSpec &spec : specs_)
    markers_.push_back({
        .active = std::nullopt,
        .beats = spec.start_beats,
        .phase = Phase::Ready,
        .queued = std::nullopt,
        .target = std::nullopt,
    });
}

namespace {

/// The rules::Fight that runs every command and query on @p encounter.
[[nodiscard]] rules::Fight bind_fight(const std::shared_ptr<Encounter> &encounter) {
  rules::Fight fight{.reach_tiles = encounter->reach_tiles()};
  for (std::size_t i = 0; i < encounter->size(); ++i)
    fight.names.push_back(encounter->spec(i).name);
  fight.clear_queued = [encounter](std::size_t actor) { encounter->clear_queued(actor); };
  fight.order_attack = [encounter](std::size_t attacker, std::size_t target) {
    encounter->order_attack(attacker, target);
  };
  fight.order_move = [encounter](std::size_t mover) { encounter->order_move(mover); };
  fight.queue_command = [encounter](std::size_t actor, std::size_t option) { encounter->queue_command(actor, option); };
  fight.update = [encounter](const rules::CombatContext &step_context, std::span<const rules::Tile> tiles) {
    EncounterStep step = encounter->update(step_context, tiles);
    return rules::FightStep{
        .reports = std::move(step.reports),
        .windups_started = std::move(step.windups_started),
    };
  };
  fight.can_act = [encounter](const rules::CombatContext &query_context, std::size_t index) {
    return encounter->can_act(query_context, index);
  };
  fight.commands = [encounter](std::size_t actor) { return encounter->commands(actor); };
  fight.health = [encounter](const rules::CombatContext &query_context, std::size_t index) {
    return encounter->health(query_context, index);
  };
  fight.outcome = [encounter] { return encounter->outcome(); };
  fight.queued = [encounter](std::size_t actor) { return encounter->queued(actor); };
  fight.target_of = [encounter](std::size_t attacker) { return encounter->target_of(attacker); };
  fight.windup = [encounter](std::size_t index) -> std::optional<rules::WindUp> {
    if (encounter->phase(index) != Phase::WindUp)
      return std::nullopt;
    return rules::WindUp{
        .is_attack = encounter->is_winding_up_at_enemy(index),
        .seconds_left = encounter->windup_seconds(index),
    };
  };
  return fight;
}

} // namespace

rules::StartFight make_flat_start_fight(std::shared_ptr<FlatCombat> combat) {
  return [combat = std::move(combat)](const rules::CombatContext &context,
                                      const rules::FightRequest &request) -> std::expected<rules::Fight, std::string> {
    std::vector<Joining> joining(1 + request.creature_ids.size());
    joining[0].side = Side::Party;
    for (std::size_t i = 0; i < request.creature_ids.size(); ++i) {
      joining[i + 1].combatant.creature = request.creature_ids[i];
      joining[i + 1].side = Side::Hostile;
    }
    const int steps_per_tick =
        std::max(1, static_cast<int>(std::lround(combat->rules.seconds_per_tick / request.step_seconds)));
    std::expected<Encounter, std::string> started =
        Encounter::start(combat->rules, context, std::move(joining), steps_per_tick);
    if (!started)
      return std::unexpected(std::move(started.error()));

    const auto encounter = std::make_shared<Encounter>(std::move(*started));
    combat->encounter = encounter;
    for (std::size_t creature = 1; creature < encounter->size(); ++creature)
      encounter->order_attack(creature, 0);
    return bind_fight(encounter);
  };
}

} // namespace corundum::test
