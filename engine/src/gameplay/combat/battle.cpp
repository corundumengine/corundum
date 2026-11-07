// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/gameplay/combat/battle.hpp>

#include <corundum/animation/animation_system.hpp>
#include <corundum/core/direction.hpp>
#include <corundum/core/math/isometric.hpp>
#include <corundum/engine.hpp>
#include <corundum/entities/entity.hpp>
#include <corundum/entities/tables/animation_table.hpp>
#include <corundum/entities/tables/clip_table.hpp>
#include <corundum/entities/world.hpp>
#include <corundum/gameplay/combat/encounter.hpp>
#include <corundum/gameplay/rules/rules.hpp>
#include <corundum/gameplay/screens/combat_hud.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/physics/physics_system.hpp>
#include <corundum/sprites/character_registry.hpp>
#include <corundum/sprites/sprite.hpp>
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

    /// Every command-bar slot's key action, in slot order: Attack first, then the commands and the
    /// toggles.
    constexpr std::array k_slot_actions{
        input::Action::CombatCommand1, input::Action::CombatCommand2, input::Action::CombatCommand3,
        input::Action::CombatCommand4, input::Action::CombatCommand5, input::Action::CombatCommand6,
        input::Action::CombatCommand7, input::Action::CombatCommand8, input::Action::CombatCommand9,
    };

    /// The slots a gamepad's A, Y and B press.
    constexpr std::size_t k_page_size = 3;

    /// How many pages @p slots need, at k_page_size slots a page.
    [[nodiscard]] int page_count(std::size_t slots) noexcept {
      return static_cast<int>((slots + k_page_size - 1) / k_page_size);
    }

    /// The button that presses @p slot while @p page shows; nullopt off the page.
    [[nodiscard]] std::optional<input::Action> gamepad_action_of(std::size_t slot, int page) noexcept {
      const std::size_t first = static_cast<std::size_t>(page) * k_page_size;
      if (slot < first || slot >= first + k_page_size)
        return std::nullopt;
      return k_slot_actions[slot - first];
    }

    /// The slot a gamepad key index presses on @p page; a keyboard key names its slot directly.
    [[nodiscard]] std::size_t slot_for_key(std::size_t key, int page, input::InputDevice device) noexcept {
      if (device == input::InputDevice::Gamepad && key < k_page_size)
        return (static_cast<std::size_t>(page) * k_page_size) + key;
      return key;
    }

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

    /// The entity each hostile in @p request names, in order; an error when one is missing or named twice.
    [[nodiscard]] std::expected<std::vector<entities::EntityId>, std::string>
    resolve_hostiles(const Engine &engine, const EncounterRequest &request) {
      std::vector<entities::EntityId> hostiles;
      for (const EncounterHostile &hostile : request.hostiles) {
        const std::optional<entities::EntityId> actor = entities::find_actor(engine.scene.world, hostile.actor_id);
        if (!actor || !engine.scene.world.transforms.has(*actor))
          return std::unexpected(
              std::format("[gameplay] start_encounter: no actor '{}' in this area", hostile.actor_id));
        if (std::ranges::find(hostiles, *actor) != hostiles.end())
          return std::unexpected(
              std::format("[gameplay] start_encounter: actor '{}' is named twice", hostile.actor_id));
        hostiles.push_back(*actor);
      }
      return hostiles;
    }

    /// One Joining per side: the protagonist first, then a hostile for each creature named.
    [[nodiscard]] std::vector<Joining> build_joining(const EncounterRequest &request) {
      std::vector<Joining> joining(1 + request.hostiles.size());
      joining[0].side = Side::Party;
      for (std::size_t i = 0; i < request.hostiles.size(); ++i) {
        joining[i + 1].combatant.creature = request.hostiles[i].creature_id;
        joining[i + 1].side = Side::Hostile;
      }
      return joining;
    }

    /// The protagonist's entity first, then one per resolved hostile.
    [[nodiscard]] std::vector<entities::EntityId> build_combatants(const Engine &engine,
                                                                   std::span<const entities::EntityId> hostiles) {
      std::vector<entities::EntityId> combatants;
      combatants.reserve(1 + hostiles.size());
      combatants.push_back(engine.scene.player);
      combatants.insert(combatants.end(), hostiles.begin(), hostiles.end());
      return combatants;
    }

    /// A clip's middle meets its command's landing, so it runs twice the wind-up: the pack's attack
    /// sheets strike around their middle frames.
    constexpr float k_clip_seconds_per_windup_second = 2.f;

    /// Whether @p frame_counts gives every facing frames: a default clip, or all eight directions.
    [[nodiscard]] bool covers_every_facing(const std::array<std::uint8_t, sprites::k_num_anim_ids> &frame_counts) {
      if (frame_counts[std::to_underlying(sprites::AnimId::Default)] > 0)
        return true;
      return std::ranges::all_of(std::span(frame_counts).first(core::k_num_directions),
                                 [](std::uint8_t count) { return count > 0; });
    }

    /// The clip @p sprite_id's character has for @p motion; nullopt with no such sheet, or one that
    /// leaves a facing with no frames.
    [[nodiscard]] std::optional<entities::Clip> find_clip(const sprites::CharacterRegistry &characters,
                                                          sprites::SpriteId sprite_id, std::string_view motion,
                                                          entities::ClipEnd end) {
      const std::optional<sprites::SpriteId> companion = characters.find_companion(sprite_id, motion);
      if (!companion)
        return std::nullopt;
      const sprites::Frames &frames = *characters.get_sprite_by_id(*companion);
      entities::Clip clip{
          .end = end,
          .frame_counts = sprites::frame_counts_of(frames),
          .frame_duration = frames.fps > 0.f ? 1.f / frames.fps : entities::k_default_frame_duration,
          .sprite = *companion,
      };
      if (!covers_every_facing(clip.frame_counts))
        return std::nullopt;
      return clip;
    }

    /// Each combatant's clips, by index. A hostile's death removes it; the protagonist's holds.
    [[nodiscard]] std::vector<CombatantClips> build_clips(const Engine &engine,
                                                          std::span<const entities::EntityId> combatants) {
      const auto &sprite_table = engine.scene.world.sprites;
      std::vector<CombatantClips> clips;
      clips.reserve(combatants.size());
      for (std::size_t i = 0; i < combatants.size(); ++i) {
        const sprites::SpriteId sprite_id = sprite_table.sprite_id[sprite_table.dense_index(combatants[i])];
        const entities::ClipEnd die_end =
            i == Battle::k_protagonist ? entities::ClipEnd::Hold : entities::ClipEnd::Remove;
        clips.push_back({
            .attack = find_clip(engine.characters, sprite_id, "attack", entities::ClipEnd::Resume),
            .die = find_clip(engine.characters, sprite_id, "die", die_end),
            .hurt = find_clip(engine.characters, sprite_id, "hurt", entities::ClipEnd::Resume),
        });
      }
      return clips;
    }

    void play(entities::World &world, entities::EntityId entity, const entities::Clip &clip) {
      animation::play_clip(world.sprites, world.animations, world.facings, world.clips, entity, clip);
    }

    /// Leave @p entity as a corpse playing @p die: gone from the fight, the paths and the dialogue,
    /// and from any lookup by actor id. The clip removes it when it ends.
    void become_corpse(entities::World &world, entities::EntityId entity, const entities::Clip &die) {
      if (world.collisions.has(entity))
        world.collisions.remove(entity);
      if (world.dialogue_refs.has(entity))
        world.dialogue_refs.remove(entity);
      if (world.actor_ids.has(entity))
        world.actor_ids.remove(entity);
      const std::uint32_t slot = world.transforms.dense_index(entity);
      world.transforms.dc[slot] = 0.f;
      world.transforms.dr[slot] = 0.f;
      play(world, entity, die);
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

    /// Copy each combatant's current numbers and status labels into @p rows; the row at @p target
    /// is marked as the fight's target.
    void refresh_rows(const Encounter &encounter, const rules::CombatContext &context,
                      std::span<screens::CombatHudRow> rows, std::size_t target) {
      const int command_point = encounter.command_point_beats();
      for (std::size_t i = 0; i < rows.size(); ++i) {
        rows[i].command_point_beats = command_point;
        rows[i].health = encounter.health(context, i);
        rows[i].is_hostile = encounter.side(i) == Side::Hostile;
        rows[i].is_target = i == target;
        rows[i].is_winding_up = encounter.phase(i) == Phase::WindUp;
        rows[i].marker_beats = encounter.beats(i);
        rows[i].status_labels = encounter.status(i).labels;
        rows[i].windup_end_beats = encounter.windup_end_beats(i);
        rows[i].windup_label = encounter.windup_label(i);
      }
    }

    /// Copy the protagonist's commands and toggles into the bar's slots.
    void refresh_slots(const Encounter &encounter, std::span<screens::CombatHudCommand> commands,
                       std::span<screens::CombatHudCommand> toggles) {
      const std::span<const rules::CommandOption> listed = encounter.commands(Battle::k_protagonist);
      for (std::size_t slot = 1; slot < commands.size(); ++slot) {
        const std::size_t option = slot - 1;
        assert(option < listed.size());
        commands[slot].label = listed[option].label;
        commands[slot].is_available = listed[option].is_available;
        commands[slot].is_highlighted = encounter.queued(Battle::k_protagonist) == option;
      }

      const std::span<const rules::ToggleOption> listed_toggles = encounter.toggles(Battle::k_protagonist);
      assert(listed_toggles.size() == toggles.size());
      for (std::size_t i = 0; i < toggles.size(); ++i) {
        toggles[i].label = listed_toggles[i].label;
        toggles[i].is_available = listed_toggles[i].is_available;
        toggles[i].is_highlighted = listed_toggles[i].is_on;
      }
    }

    /// Point each slot at the button that presses it while @p page shows; @p first is the slot's
    /// number on the bar.
    void refresh_gamepad_actions(std::span<screens::CombatHudCommand> slots, std::size_t first, int page) {
      for (std::size_t i = 0; i < slots.size(); ++i)
        slots[i].gamepad_action = gamepad_action_of(first + i, page);
    }

    /// Copy the encounter's current numbers into @p hud, in place, for the gamepad page @p page and
    /// the fight's target.
    void refresh_hud(const Encounter &encounter, const rules::CombatContext &context, screens::CombatHud &hud, int page,
                     std::size_t target) {
      refresh_rows(encounter, context, hud.rows, target);
      refresh_slots(encounter, hud.commands, hud.toggles);
      refresh_gamepad_actions(hud.commands, 0, page);
      refresh_gamepad_actions(hud.toggles, hud.commands.size(), page);
      hud.page.index = page;
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

  Battle::Battle(Encounter encounter, std::vector<std::optional<entities::EntityId>> entities,
                 std::vector<CombatantClips> clips, std::string first_actor, std::optional<rules::AutoPause> auto_pause)
      : encounter_(std::move(encounter)), entities_(std::move(entities)), clips_(std::move(clips)),
        first_actor_(std::move(first_actor)), auto_pause_(auto_pause) {
    tiles_.resize(encounter_.size());
    hud_.rows.resize(encounter_.size());
    paths_.resize(encounter_.size());
    planned_for_.resize(encounter_.size());
    for (std::size_t i = 0; i < hud_.rows.size(); ++i)
      hud_.rows[i].name = encounter_.spec(i).name;

    hud_.commands.push_back({.action = k_slot_actions[0], .label = std::string{k_attack_label}});
    for (const rules::CommandOption &command : encounter_.commands(k_protagonist)) {
      const std::size_t slot = hud_.commands.size();
      hud_.commands.push_back({.action = k_slot_actions[slot], .label = command.label});
    }
    for (const rules::ToggleOption &toggle : encounter_.toggles(k_protagonist)) {
      const std::size_t slot = hud_.commands.size() + hud_.toggles.size();
      hud_.toggles.push_back({.action = k_slot_actions[slot], .label = toggle.label});
    }
    hud_.page.count = page_count(hud_.commands.size() + hud_.toggles.size());
  }

  std::expected<Battle, std::string> Battle::start(Engine &engine, const rules::CombatRules &rules,
                                                   const EncounterRequest &request) {
    if (!world::player_present(engine.scene))
      return std::unexpected("[gameplay] start_encounter: there is no protagonist in the scene");
    if (request.hostiles.empty())
      return std::unexpected("[gameplay] start_encounter: it names no hostiles");

    std::expected<std::vector<entities::EntityId>, std::string> hostiles = resolve_hostiles(engine, request);
    if (!hostiles)
      return std::unexpected(std::move(hostiles.error()));

    const int steps_per_tick =
        std::max(1, static_cast<int>(std::lround(rules.seconds_per_tick / engine.timer.target_dt)));
    std::expected<Encounter, std::string> encounter =
        Encounter::start(rules, make_combat_context(engine), build_joining(request), steps_per_tick);
    if (!encounter)
      return std::unexpected(encounter.error());

    const std::size_t slots = 1 + encounter->commands(k_protagonist).size() + encounter->toggles(k_protagonist).size();
    if (slots > k_slot_actions.size())
      return std::unexpected(
          std::format("[gameplay] the command bar holds {} slots; the rules list {}", k_slot_actions.size(), slots));

    std::vector<entities::EntityId> combatants = build_combatants(engine, *hostiles);
    std::vector<std::optional<entities::EntityId>> entities(combatants.begin(), combatants.end());
    std::vector<CombatantClips> clips = build_clips(engine, combatants);
    Battle battle(std::move(*encounter), std::move(entities), std::move(clips), request.hostiles.front().actor_id,
                  rules.auto_pause);
    battle.open_fight(engine);
    return battle;
  }

  void Battle::open_fight(Engine &engine) {
    refresh_tiles(engine);
    const rules::CombatContext context = make_combat_context(engine);
    for (std::size_t i = 0; i < encounter_.size(); ++i)
      healths_.push_back(encounter_.health(context, i).current);
    const std::optional<std::size_t> nearest = nearest_hostile(context);
    assert(nearest.has_value());
    target_ = *nearest;
    for (std::size_t i = k_hostile; i < encounter_.size(); ++i)
      encounter_.order_attack(i, k_protagonist);
    encounter_.order_attack(k_protagonist, target_);
    refresh_hud(encounter_, context, hud_, page_, target_);
    engine.scene.is_player_held = !encounter_.status(k_protagonist).can_move;
  }

  std::vector<std::string> Battle::read_orders(const Engine &engine, const rules::CombatContext &context, Clock clock) {
    read_pages(engine);
    read_next_target(engine, context);
    std::vector<std::string> refusals = read_slot_presses(engine, context);
    read_move_orders(engine, context, clock, refusals);
    return refusals;
  }

  void Battle::read_pages(const Engine &engine) {
    if (hud_.page.count < 2)
      return;
    if (engine.input_state.is_pressed(input::Action::CombatPageNext))
      page_ = (page_ + 1) % hud_.page.count;
    if (engine.input_state.is_pressed(input::Action::CombatPagePrev))
      page_ = (page_ + hud_.page.count - 1) % hud_.page.count;
  }

  void Battle::refresh_tiles(const Engine &engine) {
    for (std::size_t i = 0; i < entities_.size(); ++i) {
      if (const std::optional<entities::EntityId> &entity = entities_[i])
        tiles_[i] = transform_tile(engine, *entity);
    }
  }

  void Battle::read_next_target(const Engine &engine, const rules::CombatContext &context) {
    if (!engine.input_state.is_pressed(input::Action::CombatTargetNext))
      return;
    const std::size_t hostiles = entities_.size() - k_hostile;
    for (std::size_t offset = 1; offset <= hostiles; ++offset) {
      const std::size_t next = k_hostile + (((target_ - k_hostile) + offset) % hostiles);
      if (!is_fighting(context, next))
        continue;
      engage(context, next);
      return;
    }
  }

  std::vector<std::string> Battle::read_slot_presses(const Engine &engine, const rules::CombatContext &context) {
    std::vector<std::string> refusals;
    const std::size_t slots = hud_.commands.size() + hud_.toggles.size();
    for (std::size_t key = 0; key < k_slot_actions.size(); ++key) {
      if (!engine.input_state.is_pressed(k_slot_actions[key]))
        continue;
      const std::size_t slot = slot_for_key(key, page_, engine.input_mapper.last_device());
      if (slot >= slots)
        continue;
      if (std::optional<std::string> refused = press_slot(context, slot))
        refusals.push_back(std::move(*refused));
    }
    return refusals;
  }

  void Battle::read_move_orders(const Engine &engine, const rules::CombatContext &context, Clock clock,
                                std::vector<std::string> &refusals) {
    const bool can_order_move = clock == Clock::Running && encounter_.status(k_protagonist).can_move;
    if (can_order_move && is_move_held(engine.input_state))
      order_protagonist_move(context, encounter_, planned_for_[k_protagonist], refusals);

    if (!engine.input_state.mouse_click_pressed || !engine.scene.hovered_tile)
      return;
    if (const std::optional<std::size_t> hostile = hostile_at(*engine.scene.hovered_tile, context)) {
      engage(context, *hostile);
      return;
    }
    if (can_order_move)
      order_protagonist_move(context, encounter_, planned_for_[k_protagonist], refusals);
  }

  void Battle::engage(const rules::CombatContext &context) {
    if (!is_fighting(context, target_))
      return;
    encounter_.order_attack(k_protagonist, target_);
    planned_for_[k_protagonist].reset();
  }

  void Battle::engage(const rules::CombatContext &context, std::size_t hostile) {
    target_ = hostile;
    engage(context);
  }

  std::optional<std::size_t> Battle::nearest_hostile(const rules::CombatContext &context) const {
    std::optional<std::size_t> nearest;
    int nearest_distance = 0;
    for (std::size_t i = k_hostile; i < entities_.size(); ++i) {
      if (!is_fighting(context, i))
        continue;
      const int distance = world::tile_distance(tiles_[i], tiles_[k_protagonist]);
      if (!nearest || distance < nearest_distance) {
        nearest = i;
        nearest_distance = distance;
      }
    }
    return nearest;
  }

  std::optional<std::size_t> Battle::hostile_at(world::TileCoord clicked, const rules::CombatContext &context) const {
    for (std::size_t i = k_hostile; i < entities_.size(); ++i) {
      if (is_fighting(context, i) && tiles_[i] == clicked)
        return i;
    }
    for (std::size_t i = k_hostile; i < entities_.size(); ++i) {
      if (is_fighting(context, i) && is_on_sprite(clicked, tiles_[i]))
        return i;
    }
    return std::nullopt;
  }

  void Battle::follow_fallen_target(const rules::CombatContext &context, AttackOrder before) {
    if (is_fighting(context, target_))
      return;
    const std::optional<std::size_t> nearest = nearest_hostile(context);
    if (!nearest)
      return;
    target_ = *nearest;
    if (before == AttackOrder::Standing)
      engage(context);
  }

  std::optional<std::string> Battle::press_slot(const rules::CombatContext &context, std::size_t slot) {
    assert(slot < hud_.commands.size() + hud_.toggles.size());

    if (slot == 0) {
      encounter_.clear_queued(k_protagonist);
      engage(context);
      return std::nullopt;
    }

    const std::size_t option = slot - 1;
    const std::span<const rules::CommandOption> commands = encounter_.commands(k_protagonist);
    if (option >= commands.size()) {
      const std::size_t toggle = option - commands.size();
      const std::span<const rules::ToggleOption> toggles = encounter_.toggles(k_protagonist);
      if (!toggles[toggle].is_on && !toggles[toggle].is_available)
        return std::format("{} can't be used now", toggles[toggle].label);
      encounter_.toggle(context, k_protagonist, toggle);
      return std::nullopt;
    }

    if (encounter_.queued(k_protagonist) == option) {
      encounter_.clear_queued(k_protagonist);
      return std::nullopt;
    }
    if (!commands[option].is_available)
      return std::format("{} can't be used now", commands[option].label);
    encounter_.queue_command(k_protagonist, option);
    if (commands[option].target == rules::CommandTarget::Enemy)
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
    if (encounter_.outcome() == Outcome::Lost) {
      hold_for_defeat(engine);
      return BattleStep{.outcome = finished_outcome(engine)};
    }

    const rules::CombatContext context = make_combat_context(engine);
    refresh_tiles(engine);

    if (clock == Clock::Running)
      pause_note_.clear();

    BattleStep step;
    step.reports = read_orders(engine, context, clock);

    if (clock == Clock::Running) {
      const AttackOrder before = encounter_.target_of(k_protagonist) ? AttackOrder::Standing : AttackOrder::None;
      EncounterStep advanced = encounter_.update(context, tiles_);
      for (std::string &report : advanced.reports)
        step.reports.push_back(std::move(report));
      play_step_clips(engine, context, advanced.windups_started);
      remove_fallen(engine, context);
      play_protagonist_death(engine, context);
      follow_fallen_target(context, before);
      step.should_pause = check_auto_pause(advanced.windups_started);
    }

    const world::MapView map = world::build_map_view(engine.render, engine.cfg);
    plan_movement(engine, map, context);
    if (clock == Clock::Running)
      walk_hostiles(engine, map, context);

    refresh_hud(encounter_, context, hud_, page_, target_);
    engine.scene.is_player_held = !encounter_.status(k_protagonist).can_move;
    // A fallen protagonist's status can still say it can move, and the next world step comes first.
    if (encounter_.outcome() == Outcome::Lost)
      hold_for_defeat(engine);
    step.outcome = finished_outcome(engine);
    return step;
  }

  void Battle::plan_movement(Engine &engine, const world::MapView &map, const rules::CombatContext &context) {
    for (std::size_t i = 0; i < entities_.size(); ++i)
      plan_combatant(engine, map, context, i);
  }

  void Battle::plan_combatant(Engine &engine, const world::MapView &map, const rules::CombatContext &context,
                              std::size_t index) {
    if (!is_fighting(context, index))
      return;
    if (!encounter_.status(index).can_move) {
      stand_still(engine, index);
      return;
    }
    const std::optional<std::size_t> target = encounter_.target_of(index);
    if (!target)
      return;

    const world::TileCoord target_tile = tiles_[*target];
    const bool in_reach = world::tile_distance(tiles_[index], target_tile) <= encounter_.spec(index).reach_tiles;
    if ((in_reach && !is_claimed(engine, tiles_[index], index)) || encounter_.phase(index) == Phase::WindUp) {
      stand_still(engine, index);
      return;
    }

    if (planned_for_[index] == target_tile)
      return;
    const ApproachOutcome outcome = approach(engine, map, index, *target);
    if (outcome == ApproachOutcome::Settled)
      planned_for_[index] = target_tile;
    else
      planned_for_[index].reset();
  }

  bool Battle::is_fighting(const rules::CombatContext &context, std::size_t index) const {
    return entities_[index].has_value() && encounter_.can_act(context, index);
  }

  bool Battle::is_claimed(const Engine &engine, world::TileCoord tile, std::size_t mover) const {
    for (std::size_t i = 0; i < entities_.size(); ++i) {
      if (i == mover || !entities_[i].has_value())
        continue;
      if (tiles_[i] == tile)
        return true;
      const std::vector<world::TileCoord> &path = encounter_.side(i) == Side::Hostile ? paths_[i] : engine.scene.path;
      if (!path.empty() && path.back() == tile)
        return true;
    }
    return false;
  }

  void Battle::remove_fallen(Engine &engine, const rules::CombatContext &context) {
    for (std::size_t i = k_hostile; i < entities_.size(); ++i) {
      const std::optional<entities::EntityId> &entity = entities_[i];
      if (!entity || encounter_.can_act(context, i))
        continue;
      if (const std::optional<entities::Clip> &die = clips_[i].die)
        become_corpse(engine.scene.world, *entity, *die);
      else
        entities::mark_for_deletion(engine.scene.world, *entity);
      paths_[i].clear();
      entities_[i].reset();
    }
  }

  void Battle::play_step_clips(Engine &engine, const rules::CombatContext &context,
                               std::span<const std::size_t> windups_started) {
    entities::World &world = engine.scene.world;
    for (const std::size_t index : windups_started)
      play_attack_clip(world, index);
    play_hurt_clips(world, context);
  }

  void Battle::play_attack_clip(entities::World &world, std::size_t index) {
    const std::optional<entities::Clip> &attack = clips_[index].attack;
    const std::optional<std::size_t> target = encounter_.target_of(index);
    const std::optional<entities::EntityId> &entity = entities_[index];
    if (!attack || !target || !entity || !encounter_.is_winding_up_at_enemy(index))
      return;

    const world::TileCoord from = tiles_[index];
    const world::TileCoord to = tiles_[*target];
    world.facings.dir_ref(*entity) =
        core::direction_from_tile_delta(static_cast<float>(to.col - from.col), static_cast<float>(to.row - from.row));

    entities::Clip timed = *attack;
    const float clip_seconds = k_clip_seconds_per_windup_second * encounter_.windup_seconds(index);
    timed.frame_duration = clip_seconds / static_cast<float>(std::ranges::max(attack->frame_counts));
    play(world, *entity, timed);
  }

  void Battle::play_hurt_clips(entities::World &world, const rules::CombatContext &context) {
    for (std::size_t i = 0; i < entities_.size(); ++i) {
      const int health = encounter_.health(context, i).current;
      const bool was_hurt = health < healths_[i];
      healths_[i] = health;

      const std::optional<entities::EntityId> &entity = entities_[i];
      const std::optional<entities::Clip> &hurt = clips_[i].hurt;
      if (!was_hurt || !entity || !hurt || !is_fighting(context, i) || encounter_.phase(i) == Phase::WindUp)
        continue;
      play(world, *entity, *hurt);
    }
  }

  void Battle::play_protagonist_death(Engine &engine, const rules::CombatContext &context) {
    const std::optional<entities::Clip> &die = clips_[k_protagonist].die;
    const std::optional<entities::EntityId> &protagonist = entities_[k_protagonist];
    if (die && protagonist && !encounter_.can_act(context, k_protagonist))
      play(engine.scene.world, *protagonist, *die);
  }

  std::optional<Outcome> Battle::finished_outcome(const Engine &engine) const {
    const std::optional<Outcome> outcome = encounter_.outcome();
    const std::optional<entities::EntityId> &protagonist = entities_[k_protagonist];
    if (outcome != Outcome::Lost || !protagonist)
      return outcome;
    const entities::ClipTable &clip_table = engine.scene.world.clips;
    const bool has_death_clip =
        clip_table.has(*protagonist) && clip_table.end_of(*protagonist) == entities::ClipEnd::Hold;
    if (has_death_clip && !clip_table.has_finished(*protagonist))
      return std::nullopt;
    return outcome;
  }

  void Battle::hold_for_defeat(Engine &engine) const {
    halt(engine);
    engine.scene.is_player_held = true;
  }

  void Battle::stand_still(Engine &engine, std::size_t index) {
    std::vector<world::TileCoord> &path = encounter_.side(index) == Side::Hostile ? paths_[index] : engine.scene.path;
    path.clear();
    planned_for_[index].reset();
  }

  Battle::ApproachOutcome Battle::approach(Engine &engine, const world::MapView &map, std::size_t mover,
                                           std::size_t target_index) {
    assert(entities_[mover].has_value());
    const std::optional<entities::EntityId> &mover_entity = entities_[mover];
    if (map.walkability == nullptr || !mover_entity)
      return ApproachOutcome::Settled;
    std::vector<world::TileCoord> &path = encounter_.side(mover) == Side::Hostile ? paths_[mover] : engine.scene.path;

    const world::TileCoord target_tile = tiles_[target_index];
    const std::vector<world::TileCoord> candidates =
        approach_candidates(target_tile, encounter_.spec(mover).reach_tiles, tiles_[mover]);

    bool skipped = false;
    for (const world::TileCoord &candidate : candidates) {
      if (is_claimed(engine, candidate, mover)) {
        skipped = true;
        continue;
      }
      if (std::vector<world::TileCoord> found =
              world::find_path(map, tiles_[mover], candidate, &engine.scene.world.collisions,
                               &engine.scene.world.transforms, *mover_entity);
          !found.empty()) {
        path = std::move(found);
        return ApproachOutcome::Settled;
      }
    }
    path.clear();
    return skipped ? ApproachOutcome::Blocked : ApproachOutcome::Settled;
  }

  void Battle::walk_hostiles(Engine &engine, const world::MapView &map, const rules::CombatContext &context) {
    const core::math::IsometricParams iso{.half_tw = map.half_tw, .half_th = map.half_th};
    auto &transforms = engine.scene.world.transforms;
    for (std::size_t i = 0; i < entities_.size(); ++i) {
      const std::optional<entities::EntityId> &entity = entities_[i];
      if (!entity || encounter_.side(i) != Side::Hostile || !is_fighting(context, i))
        continue;
      physics::follow_path(transforms, *entity, paths_[i], engine.cfg.player_speed, iso, engine.timer.target_dt);
    }
  }

  bool Battle::has_live_combatants(const Engine &engine) const {
    return std::ranges::all_of(entities_, [&engine](const std::optional<entities::EntityId> &entity) {
      return !entity || (engine.scene.world.entities.is_live(*entity) && engine.scene.world.transforms.has(*entity));
    });
  }

  void Battle::halt(Engine &engine) const {
    engine.scene.path.clear();
    auto &transforms = engine.scene.world.transforms;
    for (std::size_t i = 0; i < entities_.size(); ++i) {
      const std::optional<entities::EntityId> &entity = entities_[i];
      if (encounter_.side(i) != Side::Hostile || !entity)
        continue;
      const std::uint32_t slot = transforms.dense_index(*entity);
      transforms.dc[slot] = 0.f;
      transforms.dr[slot] = 0.f;
    }
  }

} // namespace corundum::gameplay::combat
