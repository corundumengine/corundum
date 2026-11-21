// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/gameplay/combat/battle.hpp>

#include <corundum/animation/animation_system.hpp>
#include <corundum/core/direction.hpp>
#include <corundum/core/math/isometric.hpp>
#include <corundum/core/verify.hpp>
#include <corundum/engine.hpp>
#include <corundum/entities/entity.hpp>
#include <corundum/entities/tables/animation_table.hpp>
#include <corundum/entities/tables/clip_table.hpp>
#include <corundum/entities/world.hpp>
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

    /// Every command-bar slot's key action, in slot order: Attack first, then the commands.
    constexpr std::array k_slot_actions{
        input::Action::CombatCommand1, input::Action::CombatCommand2, input::Action::CombatCommand3,
        input::Action::CombatCommand4, input::Action::CombatCommand5, input::Action::CombatCommand6,
        input::Action::CombatCommand7, input::Action::CombatCommand8, input::Action::CombatCommand9,
    };

    /// The slots a gamepad's A, Y and B press.
    constexpr std::size_t k_page_size = 3;

    /// How many pages @p slots need, at k_page_size slots a page.
    [[nodiscard]] std::size_t page_count(std::size_t slots) noexcept {
      return (slots + k_page_size - 1) / k_page_size;
    }

    /// The button that presses @p slot while @p page shows; nullopt off the page.
    [[nodiscard]] std::optional<input::Action> gamepad_action_of(std::size_t slot, std::size_t page) noexcept {
      const std::size_t first = page * k_page_size;
      if (slot < first || slot >= first + k_page_size)
        return std::nullopt;
      return k_slot_actions[slot - first];
    }

    /// The slot a gamepad key index presses on @p page; a keyboard key names its slot directly.
    [[nodiscard]] std::size_t slot_for_key(std::size_t key, std::size_t page, input::InputDevice device) noexcept {
      if (device == input::InputDevice::Gamepad && key < k_page_size)
        return (page * k_page_size) + key;
      return key;
    }

    /// Combatant 0 is the protagonist; every other combatant is a hostile.
    [[nodiscard]] constexpr bool is_hostile(std::size_t index) noexcept {
      return index >= Battle::k_hostile;
    }

    constexpr std::string_view k_stray_combatant = "[gameplay] a ruleset's fight named a combatant it doesn't have";

    constexpr std::string_view k_changed_command_list =
        "[gameplay] a ruleset's fight changed the length of its command list";

    /// @p name when @p callback is empty, and nullopt when it is set.
    template <typename Callback>
    [[nodiscard]] std::optional<std::string_view> missing_callback(std::string_view name, const Callback &callback) {
      if (callback)
        return std::nullopt;
      return name;
    }

    /// The name of the first empty callback in @p fight, in declaration order; nullopt when all are set.
    [[nodiscard]] std::optional<std::string_view> first_missing_callback(const rules::Fight &fight) {
      // The binding names every member of Fight, so a callback added there stops this compiling
      // until it is checked here.
      const auto &[names, reach_tiles, clear_queued, order_attack, order_move, queue_command, update, can_act, commands,
                   health, outcome, queued, target_of, windup] = fight;
      const std::array checks{
          missing_callback("clear_queued", clear_queued),
          missing_callback("order_attack", order_attack),
          missing_callback("order_move", order_move),
          missing_callback("queue_command", queue_command),
          missing_callback("update", update),
          missing_callback("can_act", can_act),
          missing_callback("commands", commands),
          missing_callback("health", health),
          missing_callback("outcome", outcome),
          missing_callback("queued", queued),
          missing_callback("target_of", target_of),
          missing_callback("windup", windup),
      };
      for (const std::optional<std::string_view> &check : checks) {
        if (check)
          return check;
      }
      return std::nullopt;
    }

    /// Check what Battle relies on in the fight a ruleset started for @p hostile_count hostiles.
    [[nodiscard]] std::expected<void, std::string> validate_fight(const rules::Fight &fight,
                                                                  std::size_t hostile_count) {
      if (const std::optional<std::string_view> missing = first_missing_callback(fight))
        return std::unexpected(
            std::format("[gameplay] start_encounter: the ruleset's fight has no '{}' callback", *missing));
      const std::size_t combatants = 1 + hostile_count;
      if (fight.names.size() != combatants)
        return std::unexpected(
            std::format("[gameplay] start_encounter: the ruleset's fight has {} combatants but names {}", combatants,
                        fight.names.size()));
      if (fight.reach_tiles <= 0)
        return std::unexpected("[gameplay] start_encounter: the ruleset's fight needs a positive reach_tiles");

      const std::size_t slots = 1 + fight.commands(Battle::k_protagonist).size();
      if (slots > k_slot_actions.size())
        return std::unexpected(
            std::format("[gameplay] the command bar holds {} slots; the rules list {}", k_slot_actions.size(), slots));
      return {};
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
        if (!actor)
          return std::unexpected(
              std::format("[gameplay] start_encounter: no actor '{}' in this area", hostile.actor_id));
        if (!engine.scene.world.transforms.has(*actor))
          return std::unexpected(
              std::format("[gameplay] start_encounter: actor '{}' has no position in this area", hostile.actor_id));
        if (std::ranges::find(hostiles, *actor) != hostiles.end())
          return std::unexpected(
              std::format("[gameplay] start_encounter: actor '{}' is named twice", hostile.actor_id));
        hostiles.push_back(*actor);
      }
      return hostiles;
    }

    /// The fight to ask the ruleset for: each hostile's creature, in the order named.
    [[nodiscard]] rules::FightRequest build_fight_request(const EncounterRequest &request, float step_seconds) {
      rules::FightRequest fight_request{.step_seconds = step_seconds};
      fight_request.creature_ids.reserve(request.hostiles.size());
      for (const EncounterHostile &hostile : request.hostiles)
        fight_request.creature_ids.push_back(hostile.creature_id);
      return fight_request;
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
        const std::optional<std::uint32_t> slot = sprite_table.try_dense_index(combatants[i]);
        if (!slot) {
          // An actor with no sprite row is invisible, so it has no sheet to play a clip from.
          clips.push_back({});
          continue;
        }
        const sprites::SpriteId sprite_id = sprite_table.sprite_id[*slot];
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

    /// Turn @p entity, standing on @p from, toward @p to.
    void turn_toward(entities::World &world, entities::EntityId entity, world::TileCoord from, world::TileCoord to) {
      world.facings.dir_ref(entity) =
          core::direction_from_tile_delta(static_cast<float>(to.col - from.col), static_cast<float>(to.row - from.row));
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

    /// Copy @p text into @p slot only when it changed, so a step with no new text allocates nothing.
    void assign_if_changed(std::string &slot, std::string_view text) {
      if (slot != text)
        slot = text;
    }

    /// Copy each combatant's Health and side into @p rows; the combatant at @p target is marked as
    /// the fight's target.
    void refresh_rows(const rules::Fight &fight, const rules::CombatContext &context,
                      std::span<screens::CombatHudRow> rows, std::size_t target) {
      for (std::size_t i = 0; i < rows.size(); ++i) {
        rows[i].health = fight.health(context, i);
        rows[i].is_hostile = is_hostile(i);
        rows[i].is_target = i == target;
      }
    }

    /// Copy the protagonist's commands into the bar's slots after Attack. The bar's layout and the
    /// rules' list keep one length for the whole fight (verified here), so a slot and the
    /// command behind it always line up.
    void refresh_slots(const rules::Fight &fight, std::span<screens::CombatHudCommand> slots) {
      const std::span<const rules::CommandOption> listed = fight.commands(Battle::k_protagonist);
      const std::optional<std::size_t> queued = fight.queued(Battle::k_protagonist);
      core::verify(listed.size() + 1 == slots.size(), k_changed_command_list);
      for (std::size_t option = 0; option < listed.size(); ++option) {
        screens::CombatHudCommand &command = slots[option + 1];
        assign_if_changed(command.label, listed[option].label);
        command.is_available = listed[option].is_available;
        command.is_highlighted = queued == option;
      }
    }

    /// Point each slot at the button that presses it while @p page shows.
    void refresh_gamepad_actions(std::span<screens::CombatHudCommand> slots, std::size_t page) {
      for (std::size_t i = 0; i < slots.size(); ++i)
        slots[i].gamepad_action = gamepad_action_of(i, page);
    }

    /// Copy the encounter's current numbers into @p hud, in place, for the gamepad page @p page and
    /// the fight's target.
    void refresh_hud(const rules::Fight &fight, const rules::CombatContext &context, screens::CombatHud &hud,
                     std::size_t page, std::size_t target) {
      refresh_rows(fight, context, hud.rows, target);
      refresh_slots(fight, hud.commands);
      refresh_gamepad_actions(hud.commands, page);
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

    /// Give the protagonist a ground move order, dropping any attack and its planned approach.
    void order_protagonist_move(const rules::Fight &fight, std::optional<world::TileCoord> &planned_approach) {
      fight.order_move(Battle::k_protagonist);
      planned_approach.reset();
    }

  } // namespace

  Battle::Battle(rules::Fight fight, std::vector<std::optional<entities::EntityId>> entities,
                 std::vector<CombatantClips> clips, std::string first_actor)
      : fight_(std::move(fight)), entities_(std::move(entities)), clips_(std::move(clips)),
        first_actor_(std::move(first_actor)) {
    tiles_.resize(entities_.size());
    fight_tiles_.resize(entities_.size());
    hud_.rows.resize(entities_.size());
    paths_.resize(entities_.size());
    planned_for_.resize(entities_.size());
    for (std::size_t i = 0; i < hud_.rows.size(); ++i)
      hud_.rows[i].name = fight_.names[i];

    hud_.commands.push_back({.action = k_slot_actions[0], .label = std::string{k_attack_label}});
    for (const rules::CommandOption &command : fight_.commands(k_protagonist)) {
      const std::size_t slot = hud_.commands.size();
      core::verify(slot < k_slot_actions.size(), k_changed_command_list);
      hud_.commands.push_back({.action = k_slot_actions[slot], .label = command.label});
    }
    hud_.page.count = page_count(hud_.commands.size());
  }

  std::expected<Battle, std::string> Battle::start(Engine &engine, const rules::StartFight &start_fight,
                                                   const EncounterRequest &request) {
    assert(start_fight);
    if (!world::player_present(engine.scene))
      return std::unexpected("[gameplay] start_encounter: there is no protagonist in the scene");
    if (request.hostiles.empty())
      return std::unexpected("[gameplay] start_encounter: it names no hostiles");

    std::expected<std::vector<entities::EntityId>, std::string> hostiles = resolve_hostiles(engine, request);
    if (!hostiles)
      return std::unexpected(std::move(hostiles.error()));

    std::expected<rules::Fight, std::string> fight =
        start_fight(make_combat_context(engine), build_fight_request(request, engine.timer.target_dt));
    if (!fight)
      return std::unexpected(std::format("[gameplay] start_encounter: {}", fight.error()));
    if (std::expected<void, std::string> valid = validate_fight(*fight, request.hostiles.size()); !valid)
      return std::unexpected(std::move(valid.error()));

    std::vector<entities::EntityId> combatants = build_combatants(engine, *hostiles);
    std::vector<std::optional<entities::EntityId>> entities(combatants.begin(), combatants.end());
    std::vector<CombatantClips> clips = build_clips(engine, combatants);
    Battle battle(std::move(*fight), std::move(entities), std::move(clips), request.hostiles.front().actor_id);
    battle.refresh_tiles(engine);
    const std::optional<std::size_t> nearest = battle.nearest_hostile(make_combat_context(engine));
    if (!nearest)
      return std::unexpected("[gameplay] start_encounter: no hostile in this fight can act");
    battle.open_fight(engine, *nearest);
    for (const std::optional<entities::EntityId> &entity : battle.entities_) {
      if (entity && engine.scene.world.motion_sprites.has(*entity))
        engine.scene.world.motion_sprites.enter_stance(*entity);
    }
    return battle;
  }

  void Battle::open_fight(Engine &engine, std::size_t target) {
    const rules::CombatContext context = make_combat_context(engine);
    for (std::size_t i = 0; i < entities_.size(); ++i)
      healths_.push_back(fight_.health(context, i).current);
    target_ = target;
    fight_.order_attack(k_protagonist, target_);
    refresh_hud(fight_, context, hud_, page_, target_);
  }

  void Battle::read_orders(const Engine &engine, const rules::CombatContext &context, Clock clock,
                           std::vector<std::string> &reports) {
    read_pages(engine);
    read_next_target(engine, context);
    read_slot_presses(engine, context, reports);
    read_move_orders(engine, context, clock);
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
      const std::optional<entities::EntityId> &entity = entities_[i];
      if (!entity)
        continue;
      tiles_[i] = transform_tile(engine, *entity);
      fight_tiles_[i] = {.col = tiles_[i].col, .row = tiles_[i].row};
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

  void Battle::read_slot_presses(const Engine &engine, const rules::CombatContext &context,
                                 std::vector<std::string> &refusals) {
    const std::size_t slots = hud_.commands.size();
    for (std::size_t key = 0; key < k_slot_actions.size(); ++key) {
      if (!engine.input_state.is_pressed(k_slot_actions[key]))
        continue;
      const std::size_t slot = slot_for_key(key, page_, engine.input_mapper.last_device());
      if (slot >= slots)
        continue;
      if (std::optional<std::string> refused = press_slot(context, slot))
        refusals.push_back(std::move(*refused));
    }
  }

  void Battle::read_move_orders(const Engine &engine, const rules::CombatContext &context, Clock clock) {
    const bool can_order_move = clock == Clock::Running;
    if (can_order_move && is_move_held(engine.input_state))
      order_protagonist_move(fight_, planned_for_[k_protagonist]);

    if (!engine.input_state.mouse_click_pressed || !engine.scene.hovered_tile)
      return;
    if (const std::optional<std::size_t> hostile = hostile_at(*engine.scene.hovered_tile, context)) {
      engage(context, *hostile);
      return;
    }
    if (can_order_move)
      order_protagonist_move(fight_, planned_for_[k_protagonist]);
  }

  void Battle::engage(const rules::CombatContext &context) {
    if (!is_fighting(context, target_))
      return;
    fight_.order_attack(k_protagonist, target_);
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

  std::optional<std::size_t> Battle::target_of(std::size_t index) const {
    const std::optional<std::size_t> target = fight_.target_of(index);
    core::verify(!target || *target < entities_.size(), k_stray_combatant);
    return target;
  }

  std::optional<std::string> Battle::press_slot(const rules::CombatContext &context, std::size_t slot) {
    assert(slot < hud_.commands.size());

    if (slot == 0) {
      fight_.clear_queued(k_protagonist);
      engage(context);
      return std::nullopt;
    }

    const std::size_t option = slot - 1;
    const std::span<const rules::CommandOption> commands = fight_.commands(k_protagonist);
    assert(option < commands.size());
    if (fight_.queued(k_protagonist) == option) {
      fight_.clear_queued(k_protagonist);
      return std::nullopt;
    }
    if (!commands[option].is_available)
      return std::format("{} can't be used now", commands[option].label);
    fight_.queue_command(k_protagonist, option);
    return std::nullopt;
  }

  BattleStep Battle::update(Engine &engine, Clock clock) {
    if (fight_.outcome() == rules::Outcome::Lost) {
      hold_for_defeat(engine);
      return BattleStep{.outcome = finished_outcome(engine)};
    }

    const rules::CombatContext context = make_combat_context(engine);
    refresh_tiles(engine);

    BattleStep step;
    read_orders(engine, context, clock, step.reports);

    if (clock == Clock::Running) {
      const AttackOrder before = target_of(k_protagonist) ? AttackOrder::Standing : AttackOrder::None;
      rules::FightStep advanced = fight_.update(context, fight_tiles_);
      for (const std::size_t index : advanced.windups_started)
        core::verify(index < entities_.size(), k_stray_combatant);
      for (std::string &report : advanced.reports)
        step.reports.push_back(std::move(report));
      play_step_clips(engine, context, advanced.windups_started);
      remove_fallen(engine, context);
      play_protagonist_death(engine, context);
      follow_fallen_target(context, before);

      const world::MapView map = world::build_map_view(engine.render, engine.cfg);
      plan_movement(engine, map, context);
      walk_hostiles(engine, map, context);
    } else {
      // A paused fight freezes everyone: no planning, and no path left for the world step to walk.
      stand_still(engine, k_protagonist);
    }

    refresh_hud(fight_, context, hud_, page_, target_);
    // Hold the protagonist on the step it falls: the next world step runs before the next update.
    if (fight_.outcome() == rules::Outcome::Lost)
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
    const std::optional<entities::EntityId> entity = fighting_entity(context, index);
    if (!entity)
      return;
    const std::optional<std::size_t> target = target_of(index);
    if (!target)
      return;

    const world::TileCoord target_tile = tiles_[*target];
    const bool in_reach = world::tile_distance(tiles_[index], target_tile) <= fight_.reach_tiles;
    if ((in_reach && !is_claimed(engine, tiles_[index], index)) || fight_.windup(index).has_value()) {
      stand_still(engine, index);
      return;
    }

    if (planned_for_[index] == target_tile)
      return;
    const ApproachOutcome outcome = approach(engine, map, index, *entity, *target);
    if (outcome == ApproachOutcome::Settled)
      planned_for_[index] = target_tile;
    else
      planned_for_[index].reset();
  }

  std::optional<entities::EntityId> Battle::fighting_entity(const rules::CombatContext &context,
                                                            std::size_t index) const {
    if (!entities_[index] || !fight_.can_act(context, index))
      return std::nullopt;
    return entities_[index];
  }

  bool Battle::is_fighting(const rules::CombatContext &context, std::size_t index) const {
    return fighting_entity(context, index).has_value();
  }

  bool Battle::is_claimed(const Engine &engine, world::TileCoord tile, std::size_t mover) const {
    for (std::size_t i = 0; i < entities_.size(); ++i) {
      if (i == mover || !entities_[i].has_value())
        continue;
      if (tiles_[i] == tile)
        return true;
      const std::vector<world::TileCoord> &path = is_hostile(i) ? paths_[i] : engine.scene.path;
      if (!path.empty() && path.back() == tile)
        return true;
    }
    return false;
  }

  void Battle::remove_fallen(Engine &engine, const rules::CombatContext &context) {
    for (std::size_t i = k_hostile; i < entities_.size(); ++i) {
      const std::optional<entities::EntityId> &entity = entities_[i];
      if (!entity || fight_.can_act(context, i))
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
    const std::optional<std::size_t> target = target_of(index);
    const std::optional<entities::EntityId> &entity = entities_[index];
    const std::optional<rules::WindUp> windup = fight_.windup(index);
    const bool is_attack_winding_up = windup && windup->is_attack;
    if (!attack || !target || !entity || !is_attack_winding_up)
      return;

    turn_toward(world, *entity, tiles_[index], tiles_[*target]);

    entities::Clip timed = *attack;
    const float clip_seconds = k_clip_seconds_per_windup_second * windup->seconds_left;
    timed.frame_duration = clip_seconds / static_cast<float>(std::ranges::max(attack->frame_counts));
    play(world, *entity, timed);
  }

  void Battle::play_hurt_clips(entities::World &world, const rules::CombatContext &context) {
    for (std::size_t i = 0; i < entities_.size(); ++i) {
      const int health = fight_.health(context, i).current;
      const bool was_hurt = health < healths_[i];
      healths_[i] = health;

      const std::optional<entities::EntityId> &entity = entities_[i];
      const std::optional<entities::Clip> &hurt = clips_[i].hurt;
      if (!was_hurt || !entity || !hurt || !is_fighting(context, i) || fight_.windup(i).has_value())
        continue;
      play(world, *entity, *hurt);
    }
  }

  void Battle::play_protagonist_death(Engine &engine, const rules::CombatContext &context) {
    const std::optional<entities::Clip> &die = clips_[k_protagonist].die;
    const std::optional<entities::EntityId> &protagonist = entities_[k_protagonist];
    if (die && protagonist && !fight_.can_act(context, k_protagonist))
      play(engine.scene.world, *protagonist, *die);
  }

  std::optional<rules::Outcome> Battle::finished_outcome(const Engine &engine) const {
    const std::optional<rules::Outcome> outcome = fight_.outcome();
    const std::optional<entities::EntityId> &protagonist = entities_[k_protagonist];
    if (outcome != rules::Outcome::Lost || !protagonist)
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
    std::vector<world::TileCoord> &path = is_hostile(index) ? paths_[index] : engine.scene.path;
    path.clear();
    planned_for_[index].reset();
  }

  Battle::ApproachOutcome Battle::approach(Engine &engine, const world::MapView &map, std::size_t mover,
                                           entities::EntityId mover_entity, std::size_t target_index) {
    if (map.walkability == nullptr)
      return ApproachOutcome::NoMap;
    std::vector<world::TileCoord> &path = is_hostile(mover) ? paths_[mover] : engine.scene.path;

    const world::TileCoord target_tile = tiles_[target_index];
    const std::vector<world::TileCoord> candidates =
        approach_candidates(target_tile, fight_.reach_tiles, tiles_[mover]);

    bool skipped = false;
    for (const world::TileCoord &candidate : candidates) {
      if (is_claimed(engine, candidate, mover)) {
        skipped = true;
        continue;
      }
      if (std::vector<world::TileCoord> found =
              world::find_path(map, tiles_[mover], candidate, &engine.scene.world.collisions,
                               &engine.scene.world.transforms, mover_entity);
          !found.empty()) {
        path = std::move(found);
        return ApproachOutcome::Settled;
      }
    }
    path.clear();
    return skipped ? ApproachOutcome::Blocked : ApproachOutcome::Settled;
  }

  void Battle::walk_hostiles(Engine &engine, const world::MapView &map, const rules::CombatContext &context) {
    const core::math::IsometricParams iso{.half_tw = map.projection.half_tw, .half_th = map.projection.half_th};
    auto &transforms = engine.scene.world.transforms;
    for (std::size_t i = 0; i < entities_.size(); ++i) {
      const std::optional<entities::EntityId> &entity = entities_[i];
      if (!entity || !is_hostile(i) || !is_fighting(context, i))
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
      if (!is_hostile(i) || !entity)
        continue;
      const std::uint32_t slot = transforms.dense_index(*entity);
      transforms.dc[slot] = 0.f;
      transforms.dr[slot] = 0.f;
    }
  }

  void Battle::stand_down(Engine &engine) const {
    entities::MotionSpriteTable &motion_sprites = engine.scene.world.motion_sprites;
    for (const std::optional<entities::EntityId> &entity : entities_) {
      if (entity && motion_sprites.has(*entity))
        motion_sprites.leave_stance(*entity);
    }
  }

} // namespace corundum::gameplay::combat
