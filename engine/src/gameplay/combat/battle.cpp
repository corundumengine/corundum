// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/gameplay/combat/battle.hpp>

#include <corundum/engine.hpp>
#include <corundum/entities/entity.hpp>
#include <corundum/entities/world.hpp>
#include <corundum/gameplay/combat/encounter.hpp>
#include <corundum/gameplay/rules/rules.hpp>
#include <corundum/gameplay/screens/combat_hud.hpp>
#include <corundum/world/flags.hpp>
#include <corundum/world/grid.hpp>
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

    /// Turn this step's click on a hostile's tile into the protagonist's standing attack.
    void read_orders(Encounter &encounter, const rules::CombatContext &context, const Engine &engine,
                     std::span<const world::TileCoord> tiles) {
      if (!engine.input_state.mouse_click_pressed || !engine.scene.hovered_tile)
        return;
      for (std::size_t i = 0; i < tiles.size(); ++i) {
        if (encounter.side(i) == Side::Hostile && tiles[i] == *engine.scene.hovered_tile &&
            encounter.can_act(context, i)) {
          encounter.order_attack(Battle::k_protagonist, i);
          return;
        }
      }
    }

  } // namespace

  Battle::Battle(Encounter encounter, std::vector<entities::EntityId> entities, std::string actor_id)
      : encounter_(std::move(encounter)), entities_(std::move(entities)), actor_id_(std::move(actor_id)) {
    tiles_.resize(encounter_.size());
    hud_rows_.resize(encounter_.size());
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

    read_orders(encounter_, context, engine, tiles_);

    BattleStep step;
    if (clock == Clock::Running)
      step.reports = encounter_.update(context, tiles_);

    refresh_hud(encounter_, context, hud_rows_);
    step.outcome = encounter_.outcome();
    return step;
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
