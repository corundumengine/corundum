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

    /// Copy the encounter's current numbers into @p rows, in place.
    void refresh_hud(const Encounter &encounter, const rules::CombatContext &context,
                     std::span<screens::CombatHudRow> rows) {
      const int command_point = encounter.command_point_beats();
      for (std::size_t i = 0; i < rows.size(); ++i) {
        rows[i].command_point_beats = command_point;
        rows[i].health = encounter.health(context, i);
        rows[i].is_hostile = encounter.side(i) == Side::Hostile;
        rows[i].is_winding_up = encounter.phase(i) == Phase::WindUp;
        rows[i].marker_beats = encounter.beats(i);
        rows[i].windup_end_beats = command_point + encounter.spec(i).attack.windup_beats;
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
          if (world::tile_distance(candidate, target) >= 1)
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

    /// Give the protagonist a ground move order, dropping any attack and its planned approach.
    void order_protagonist_move(Encounter &encounter, std::span<std::optional<world::TileCoord>> planned_for) {
      encounter.order_move(Battle::k_protagonist);
      planned_for[Battle::k_protagonist].reset();
    }

    /// Turn this step's click into the protagonist's attack, ground move or nothing, and a held
    /// movement key into a ground move.
    void read_orders(Encounter &encounter, const rules::CombatContext &context, const Engine &engine, Clock clock,
                     std::span<const world::TileCoord> tiles, std::span<std::optional<world::TileCoord>> planned_for) {
      const bool is_running = clock == Clock::Running;
      if (is_running && is_move_held(engine.input_state))
        order_protagonist_move(encounter, planned_for);

      if (!engine.input_state.mouse_click_pressed || !engine.scene.hovered_tile)
        return;
      for (std::size_t i = 0; i < tiles.size(); ++i) {
        if (encounter.side(i) == Side::Hostile && tiles[i] == *engine.scene.hovered_tile &&
            encounter.can_act(context, i)) {
          encounter.order_attack(Battle::k_protagonist, i);
          planned_for[Battle::k_protagonist].reset();
          return;
        }
      }
      if (is_running)
        order_protagonist_move(encounter, planned_for);
    }

  } // namespace

  Battle::Battle(Encounter encounter, std::vector<entities::EntityId> entities, std::string actor_id)
      : encounter_(std::move(encounter)), entities_(std::move(entities)), actor_id_(std::move(actor_id)) {
    tiles_.resize(encounter_.size());
    hud_rows_.resize(encounter_.size());
    paths_.resize(encounter_.size());
    planned_for_.resize(encounter_.size());
    for (std::size_t i = 0; i < hud_rows_.size(); ++i)
      hud_rows_[i].name = encounter_.spec(i).name;
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

    Battle battle(std::move(*encounter), {engine.scene.player, *actor}, request.actor_id);
    battle.encounter_.order_attack(k_hostile, k_protagonist);
    refresh_hud(battle.encounter_, make_combat_context(engine), battle.hud_rows_);
    return battle;
  }

  BattleStep Battle::update(Engine &engine, Clock clock) {
    const rules::CombatContext context = make_combat_context(engine);
    for (std::size_t i = 0; i < entities_.size(); ++i)
      tiles_[i] = transform_tile(engine, entities_[i]);

    read_orders(encounter_, context, engine, clock, tiles_, planned_for_);

    BattleStep step;
    if (clock == Clock::Running)
      step.reports = encounter_.update(context, tiles_);

    const world::MapView map = world::build_map_view(engine.render, engine.cfg);
    plan_movement(engine, map, context);
    if (clock == Clock::Running)
      walk_hostiles(engine, map, context);

    refresh_hud(encounter_, context, hud_rows_);
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
        if (encounter_.side(i) == Side::Hostile)
          paths_[i].clear();
        else
          engine.scene.path.clear();
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
      std::vector<world::TileCoord> found =
          world::find_path(map, tiles_[mover], candidate, &engine.scene.world.collisions,
                           &engine.scene.world.transforms, entities_[mover]);
      if (!found.empty()) {
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
