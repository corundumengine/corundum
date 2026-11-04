// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/gameplay/combat/battle.hpp>

#include <corundum/core/math/isometric.hpp>
#include <corundum/engine.hpp>
#include <corundum/entities/entity.hpp>
#include <corundum/entities/world.hpp>
#include <corundum/gameplay/combat/encounter.hpp>
#include <corundum/gameplay/rules/rules.hpp>
#include <corundum/gameplay/screens/combat_hud.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/physics/physics_system.hpp>
#include <corundum/world/flags.hpp>
#include <corundum/world/grid.hpp>
#include <corundum/world/map_view.hpp>
#include <corundum/world/pathfinding.hpp>
#include <corundum/world/picking.hpp>
#include <corundum/world/scene.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
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

    /// The actions for command-bar slots 2 and 3; slot 1 is always the standing attack.
    constexpr std::array k_command_actions{
        input::Action::CombatCommand2,
        input::Action::CombatCommand3,
    };

    /// The rules' window into the engine: its seeded dice and the playthrough's flags.
    rules::CombatContext make_combat_context(Engine &engine) {
      const auto roll = [&engine](int sides) {
        assert(sides >= 1);
        return static_cast<int>(engine.rng.range(1, sides));
      };
      const auto write = [&engine](std::string_view key, int value) { engine.flags[key] = value; };
      const auto read = [&engine](std::string_view key) { return world::visit_count(engine.flags, key); };
      return rules::CombatContext{
          .dice = rules::Dice{.roll = roll},
          .flag_writer = rules::FlagWriter{.write = write},
          .flags = rules::FlagReader{.read = read},
      };
    }

    /// The tile an entity stands on.
    [[nodiscard]] world::TileCoord transform_tile(const Engine &engine, entities::EntityId entity) {
      const auto &transforms = engine.scene.world.transforms;
      return world::tile_of(transforms.pos_col(entity), transforms.pos_row(entity));
    }

    /// Whether a click on @p clicked is a click on a combatant standing on @p feet: its own tile,
    /// or one of the three tiles its sprite is drawn over, one column back, one row back, or both
    /// (a column or row back sits above it on screen, to the left or right; both, straight above).
    [[nodiscard]] bool is_on_sprite(world::TileCoord clicked, world::TileCoord feet) noexcept {
      const int columns_back = feet.col - clicked.col;
      const int rows_back = feet.row - clicked.row;
      return (columns_back == 0 || columns_back == 1) && (rows_back == 0 || rows_back == 1);
    }

    /// Copy the encounter's current numbers into @p hud, in place.
    void refresh_hud(const Encounter &encounter, const rules::CombatContext &context, screens::CombatHud &hud) {
      const int command_point = encounter.command_point_beats();
      for (std::size_t i = 0; i < hud.rows.size(); ++i) {
        hud.rows[i].command_point_beats = command_point;
        hud.rows[i].health = encounter.health(context, i);
        hud.rows[i].is_hostile = encounter.side(i) == Side::Hostile;
        hud.rows[i].is_winding_up = encounter.phase(i) == Phase::WindUp;
        hud.rows[i].marker_beats = encounter.beats(i);
        hud.rows[i].windup_end_beats = encounter.windup_end_beats(i);
        hud.rows[i].windup_label = encounter.windup_label(i);
      }

      const std::span<const rules::CommandOption> listed = encounter.commands(Battle::k_protagonist);
      for (std::size_t slot = 1; slot < hud.commands.size(); ++slot) {
        const std::size_t option = slot - 1;
        assert(option < listed.size());
        hud.commands[slot].label = listed[option].label;
        hud.commands[slot].is_available = listed[option].is_available;
        hud.commands[slot].is_queued = encounter.queued(Battle::k_protagonist) == option;
      }
    }

    /// The squared Euclidean distance between two tiles, for ordering approach candidates.
    [[nodiscard]] int squared_tile_distance(world::TileCoord a, world::TileCoord b) noexcept {
      const int dc = a.col - b.col;
      const int dr = a.row - b.row;
      return (dc * dc) + (dr * dr);
    }

    /// Every tile within @p reach of @p target but not on it, nearest to @p from first, then by
    /// column and row.
    [[nodiscard]] std::vector<world::TileCoord> approach_candidates(world::TileCoord target, int reach,
                                                                    world::TileCoord from) {
      std::vector<world::TileCoord> candidates;
      for (int col = target.col - reach; col <= target.col + reach; ++col) {
        for (int row = target.row - reach; row <= target.row + reach; ++row) {
          const world::TileCoord candidate{.col = col, .row = row};
          if (candidate != target)
            candidates.push_back(candidate);
        }
      }
      std::ranges::sort(candidates, [from](world::TileCoord a, world::TileCoord b) {
        const int distance_a = squared_tile_distance(a, from);
        const int distance_b = squared_tile_distance(b, from);
        if (distance_a != distance_b)
          return distance_a < distance_b;
        if (a.col != b.col)
          return a.col < b.col;
        return a.row < b.row;
      });
      return candidates;
    }

    /// Whether any of the four movement keys is held this step.
    [[nodiscard]] bool is_move_held(const input::InputState &input) noexcept {
      return input.is_held(input::Action::MoveUp) || input.is_held(input::Action::MoveDown) ||
             input.is_held(input::Action::MoveLeft) || input.is_held(input::Action::MoveRight);
    }

    /// Give the protagonist a ground move order, dropping any attack and its planned approach, and
    /// append the end-of-turn reports of a wind-up it cancelled.
    void order_protagonist_move(const rules::CombatContext &context, Encounter &encounter,
                                std::optional<world::TileCoord> &planned_approach, std::vector<std::string> &reports) {
      for (std::string &report : encounter.order_move(context, Battle::k_protagonist))
        reports.push_back(std::move(report));
      planned_approach.reset();
    }

  } // namespace

  Battle::Battle(Encounter encounter, std::vector<entities::EntityId> entities, std::string actor_id,
                 std::optional<rules::AutoPause> auto_pause)
      : encounter_(std::move(encounter)), entities_(std::move(entities)), actor_id_(std::move(actor_id)),
        auto_pause_(auto_pause) {
    tiles_.resize(encounter_.size());
    hud_.rows.resize(encounter_.size());
    paths_.resize(encounter_.size());
    planned_for_.resize(encounter_.size());
    for (std::size_t i = 0; i < hud_.rows.size(); ++i)
      hud_.rows[i].name = encounter_.spec(i).name;

    hud_.commands.push_back({.action = input::Action::CombatCommand1, .label = std::string{k_attack_label}});
    const std::span<const rules::CommandOption> listed = encounter_.commands(k_protagonist);
    for (std::size_t i = 0; i < std::min(listed.size(), k_command_actions.size()); ++i)
      hud_.commands.push_back({.action = k_command_actions[i], .label = listed[i].label});
  }

  std::expected<Battle, std::string> Battle::start(Engine &engine, const rules::CombatRules &rules,
                                                   const EncounterRequest &request) {
    if (!world::player_present(engine.scene))
      return std::unexpected("[gameplay] start_encounter: there is no protagonist in the scene");

    const std::optional<entities::EntityId> actor = entities::find_actor(engine.scene.world, request.actor_id);
    if (!actor || !engine.scene.world.transforms.has(*actor))
      return std::unexpected(std::format("[gameplay] start_encounter: no actor '{}' in this area", request.actor_id));

    const int steps_per_tick =
        std::max(1, static_cast<int>(std::lround(rules.seconds_per_tick / engine.timer.target_dt)));
    std::vector<Joining> joining(2);
    joining[0].side = Side::Party;
    joining[1].combatant.creature = request.creature_id;
    joining[1].side = Side::Hostile;
    std::expected<Encounter, std::string> encounter =
        Encounter::start(rules, make_combat_context(engine), std::move(joining), steps_per_tick);
    if (!encounter)
      return std::unexpected(encounter.error());

    Battle battle(std::move(*encounter), {engine.scene.player, *actor}, request.actor_id, rules.auto_pause);
    battle.encounter_.order_attack(k_hostile, k_protagonist);
    battle.encounter_.order_attack(k_protagonist, k_hostile);
    refresh_hud(battle.encounter_, make_combat_context(engine), battle.hud_);
    return battle;
  }

  std::vector<std::string> Battle::read_orders(const Engine &engine, const rules::CombatContext &context, Clock clock) {
    std::vector<std::string> refusals;
    const bool is_running = clock == Clock::Running;
    if (is_running && is_move_held(engine.input_state))
      order_protagonist_move(context, encounter_, planned_for_[k_protagonist], refusals);

    if (engine.input_state.is_pressed(input::Action::CombatCommand1)) {
      encounter_.clear_queued(k_protagonist);
      engage(context);
    }
    for (std::size_t slot = 1; slot < hud_.commands.size(); ++slot) {
      if (!engine.input_state.is_pressed(hud_.commands[slot].action))
        continue;
      if (std::optional<std::string> refused = press_slot(context, slot))
        refusals.push_back(std::move(*refused));
    }

    if (!engine.input_state.mouse_click_pressed || !engine.scene.hovered_tile)
      return refusals;
    if (is_on_sprite(*engine.scene.hovered_tile, tiles_[k_hostile]) && encounter_.can_act(context, k_hostile)) {
      engage(context);
      return refusals;
    }
    if (is_running)
      order_protagonist_move(context, encounter_, planned_for_[k_protagonist], refusals);
    return refusals;
  }

  void Battle::engage(const rules::CombatContext &context) {
    if (!encounter_.can_act(context, k_hostile))
      return;
    encounter_.order_attack(k_protagonist, k_hostile);
    planned_for_[k_protagonist].reset();
  }

  std::optional<std::string> Battle::press_slot(const rules::CombatContext &context, std::size_t slot) {
    const std::span<const rules::CommandOption> listed = encounter_.commands(k_protagonist);
    const std::size_t option = slot - 1;
    if (option >= listed.size())
      return std::nullopt;
    if (encounter_.queued(k_protagonist) == option) {
      encounter_.clear_queued(k_protagonist);
      return std::nullopt;
    }
    if (!listed[option].is_available)
      return std::format("{} can't be used now", listed[option].label);
    encounter_.queue_command(k_protagonist, option);
    if (listed[option].target == rules::CommandTarget::Enemy)
      engage(context);
    return std::nullopt;
  }

  bool Battle::check_auto_pause(std::span<const std::size_t> started) {
    if (!auto_pause_)
      return false;
    const auto found = std::ranges::find_if(started, [this](std::size_t index) {
      // A Break later in the same tick can already have cancelled the wind-up.
      if (encounter_.side(index) != Side::Hostile || encounter_.phase(index) != Phase::WindUp)
        return false;
      const int windup_beats = encounter_.windup_end_beats(index) - encounter_.command_point_beats();
      if (windup_beats < auto_pause_->min_windup_beats)
        return false;
      return world::tile_distance(tiles_[index], tiles_[k_protagonist]) <= auto_pause_->max_distance_tiles;
    });
    if (found == started.end())
      return false;
    pause_note_ = std::format("{} is winding up {}", encounter_.spec(*found).name, encounter_.windup_label(*found));
    return true;
  }

  BattleStep Battle::update(Engine &engine, Clock clock) {
    const rules::CombatContext context = make_combat_context(engine);
    for (std::size_t i = 0; i < entities_.size(); ++i)
      tiles_[i] = transform_tile(engine, entities_[i]);

    if (clock == Clock::Running)
      pause_note_.clear();

    BattleStep step;
    step.reports = read_orders(engine, context, clock);

    if (clock == Clock::Running) {
      EncounterStep advanced = encounter_.update(context, tiles_);
      for (std::string &report : advanced.reports)
        step.reports.push_back(std::move(report));
      step.should_pause = check_auto_pause(advanced.windups_started);
    }

    const world::MapView map = world::build_map_view(engine.render, engine.cfg);
    plan_movement(engine, map, context);
    if (clock == Clock::Running)
      walk_hostiles(engine, map, context);

    refresh_hud(encounter_, context, hud_);
    step.outcome = encounter_.outcome();
    return step;
  }

  void Battle::plan_movement(Engine &engine, const world::MapView &map, const rules::CombatContext &context) {
    for (std::size_t i = 0; i < entities_.size(); ++i) {
      if (!encounter_.can_act(context, i))
        continue;
      const std::optional<std::size_t> target = encounter_.target_of(i);
      if (!target)
        continue;

      const world::TileCoord target_tile = tiles_[*target];
      const bool in_reach = world::tile_distance(tiles_[i], target_tile) <= encounter_.spec(i).reach_tiles;
      if (in_reach || encounter_.phase(i) == Phase::WindUp) {
        std::vector<world::TileCoord> &path = encounter_.side(i) == Side::Hostile ? paths_[i] : engine.scene.path;
        path.clear();
        planned_for_[i].reset();
        continue;
      }

      if (planned_for_[i] == target_tile)
        continue;
      approach(engine, map, i, *target);
      planned_for_[i] = target_tile;
    }
  }

  void Battle::approach(Engine &engine, const world::MapView &map, std::size_t mover, std::size_t target_index) {
    if (map.walkability == nullptr)
      return;
    std::vector<world::TileCoord> &path = encounter_.side(mover) == Side::Hostile ? paths_[mover] : engine.scene.path;

    const world::TileCoord target_tile = tiles_[target_index];
    const std::vector<world::TileCoord> candidates =
        approach_candidates(target_tile, encounter_.spec(mover).reach_tiles, tiles_[mover]);

    for (const world::TileCoord &candidate : candidates) {
      if (std::vector<world::TileCoord> found =
              world::find_path(map, tiles_[mover], candidate, &engine.scene.world.collisions,
                               &engine.scene.world.transforms, entities_[mover]);
          !found.empty()) {
        path = std::move(found);
        return;
      }
    }
    path.clear();
  }

  void Battle::walk_hostiles(Engine &engine, const world::MapView &map, const rules::CombatContext &context) {
    const core::math::IsometricParams iso{.half_tw = map.half_tw, .half_th = map.half_th};
    auto &transforms = engine.scene.world.transforms;
    for (std::size_t i = 0; i < entities_.size(); ++i) {
      if (encounter_.side(i) != Side::Hostile || !encounter_.can_act(context, i))
        continue;
      physics::follow_path(transforms, entities_[i], paths_[i], engine.cfg.player_speed, iso, engine.timer.target_dt);
    }
  }

  bool Battle::has_live_combatants(const Engine &engine) const {
    return std::ranges::all_of(entities_, [&engine](entities::EntityId entity) {
      return engine.scene.world.entities.is_live(entity) && engine.scene.world.transforms.has(entity);
    });
  }

  void Battle::halt(Engine &engine) const {
    engine.scene.path.clear();
    auto &transforms = engine.scene.world.transforms;
    for (std::size_t i = 0; i < entities_.size(); ++i) {
      if (encounter_.side(i) != Side::Hostile)
        continue;
      const std::uint32_t slot = transforms.dense_index(entities_[i]);
      transforms.dc[slot] = 0.f;
      transforms.dr[slot] = 0.f;
    }
  }

} // namespace corundum::gameplay::combat
