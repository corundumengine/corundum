// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/animation/animation_system.hpp>
#include <corundum/core/direction.hpp>
#include <corundum/engine.hpp>
#include <corundum/entities/components.hpp>
#include <corundum/entities/entity.hpp>
#include <corundum/entities/tables/clip_table.hpp>
#include <corundum/entities/world.hpp>
#include <corundum/gameplay/combat/battle.hpp>
#include <corundum/gameplay/combat/encounter.hpp>
#include <corundum/gameplay/dialogue/action.hpp>
#include <corundum/gameplay/gameplay.hpp>
#include <corundum/gameplay/rules/rules.hpp>
#include <corundum/gameplay/screens/combat_hud.hpp>
#include <corundum/gameplay/screens/modes.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/physics/physics_system.hpp>
#include <corundum/screen_registry.hpp>
#include <corundum/sprites/sprite.hpp>
#include <corundum/world/flags.hpp>
#include <corundum/world/grid.hpp>
#include <corundum/world/map_view.hpp>
#include <corundum/world/picking.hpp>
#include <corundum/world/ui_stack.hpp>

#include "gameplay/rules/flat_ruleset.hpp"
#include "ui/recording_renderer.hpp"
#include "world_transition_fixtures.hpp"

#include <cstddef>
#include <expected>
#include <format>
#include <initializer_list>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

  namespace combat = corundum::gameplay::combat;
  namespace screens = corundum::gameplay::screens;
  using corundum::entities::EntityId;
  using corundum::gameplay::dialogue::EventAction;
  using corundum::input::Action;
  using corundum::world::TileCoord;

  constexpr float k_dt = 1.f / 60.f;

  /// A booted fixture world with the gameplay framework, the flat combat rules, the protagonist on
  /// (8, 8) and actor a00 (the foe) moved next to it or further off. Events that reach on_event are
  /// recorded in `raised`.
  struct Fight {
    std::unique_ptr<corundum::Engine> engine{};

    std::unique_ptr<corundum::gameplay::Gameplay> gameplay{};

    EntityId foe{};

    std::vector<EventAction> raised{};
  };

  void place(corundum::Engine &engine, EntityId entity, TileCoord tile) {
    auto &transforms = engine.scene.world.transforms;
    const auto slot = transforms.dense_index(entity);
    transforms.col[slot] = static_cast<float>(tile.col);
    transforms.row[slot] = static_cast<float>(tile.row);
  }

  std::unique_ptr<Fight> make_fight(TileCoord foe_tile) {
    auto fight = std::make_unique<Fight>();
    fight->engine = std::make_unique<corundum::Engine>();
    corundum::test::adopt_platform(*fight->engine, 320, 240);
    REQUIRE(corundum::test::initialize_with_world(
                *fight->engine, corundum::test::make_world_config(CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR))
                .has_value());
    fight->gameplay = std::make_unique<corundum::gameplay::Gameplay>(*fight->engine);
    fight->gameplay->ruleset.combat = corundum::test::make_flat_combat_rules();
    fight->gameplay->on_event = [raised = &fight->raised](corundum::Engine &, const EventAction &event) {
      raised->push_back(event);
      return true;
    };
    const std::optional<EntityId> foe = corundum::entities::find_actor(fight->engine->scene.world, "a00");
    if (!foe) {
      FAIL("the fixture world has no actor a00");
      return fight;
    }
    fight->foe = *foe;
    place(*fight->engine, fight->engine->scene.player, {.col = 8, .row = 8});
    place(*fight->engine, fight->foe, foe_tile);
    return fight;
  }

  void step(Fight &fight) {
    fight.gameplay->fixed_step(k_dt);
    corundum::entities::flush_deletions(fight.engine->scene.world);
    fight.engine->input_state = {};
  }

  void press(Fight &fight, Action action) {
    fight.engine->input_state.pressed.set(static_cast<std::size_t>(action));
    step(fight);
  }

  void click(Fight &fight, TileCoord tile) {
    fight.engine->scene.hovered_tile = tile;
    fight.engine->input_state.mouse_click_pressed = true;
    step(fight);
  }

  /// Press and release gamepad @p control through the input mapper, so the last device is the gamepad.
  void press_gamepad(Fight &fight, corundum::input::GamepadControl control) {
    corundum::input::GamepadState pad{};
    pad.buttons[static_cast<std::size_t>(control)] = true;
    fight.engine->input_mapper.begin_poll();
    fight.engine->input_mapper.gamepad(pad);
    fight.engine->input_mapper.end_poll(fight.engine->input_state);
    step(fight);
    fight.engine->input_mapper.begin_poll();
    fight.engine->input_mapper.gamepad(corundum::input::GamepadState{});
    fight.engine->input_mapper.end_poll(fight.engine->input_state);
    fight.engine->input_state = {};
  }

  /// Ask for a fight with these start_encounter arguments, as a dialogue action does, and run the
  /// step that starts it.
  void start_with_args(Fight &fight, std::vector<std::string> args) {
    fight.gameplay->pending_dialogue_events.push_back({.name = "start_encounter", .args = std::move(args)});
    step(fight);
  }

  /// Ask for a fight with a00 as @p creature, and run the step that starts it.
  void start_fight(Fight &fight, std::string creature = "grunt") {
    start_with_args(fight, {"a00", std::move(creature)});
  }

  /// Ask for a fight with a00 as "first" and a01 as "second", and run the step that starts it.
  void start_pair(Fight &fight) {
    start_with_args(fight, {"a00", "first", "a01", "second"});
  }

  /// Step until the fight ends; capped, so a fight that never ends fails the checks that follow.
  void step_until_over(Fight &fight) {
    for (int i = 0; i < 40 && fight.gameplay->battle; ++i)
      step(fight);
  }

  /// Step until the fight pauses itself; capped, so a fight that never pauses fails the checks that follow.
  void step_until_paused(Fight &fight) {
    for (int i = 0; i < 10 && fight.engine->scene.mode() == screens::Combat; ++i)
      step(fight);
  }

  const combat::Encounter &encounter_of(const Fight &fight) {
    if (!fight.gameplay->battle)
      throw std::runtime_error("no fight is running");
    return fight.gameplay->battle->encounter();
  }

  const combat::Battle &battle_of(const Fight &fight) {
    if (!fight.gameplay->battle)
      throw std::runtime_error("no fight is running");
    return *fight.gameplay->battle;
  }

  /// The fight's combat rules; make_fight always installs the flat ruleset.
  corundum::gameplay::rules::CombatRules &combat_rules(Fight &fight) {
    if (!fight.gameplay->ruleset.combat)
      throw std::runtime_error("the flat ruleset has no combat rules");
    return *fight.gameplay->ruleset.combat;
  }

  std::string last_toast(const corundum::Engine &engine) {
    REQUIRE_FALSE(engine.toasts.empty());
    return engine.toasts.at(engine.toasts.size() - 1).text;
  }

  /// Put @p entity on @p idle_sprite; Battle::start finds its clips from its sprite.
  void dress_as(Fight &fight, EntityId entity, const std::string &idle_sprite) {
    auto &world = fight.engine->scene.world;
    const corundum::sprites::Frames *idle = fight.engine->characters.get_sprite(idle_sprite);
    REQUIRE(idle != nullptr);
    world.sprites.sprite_id_ref(entity) = idle->sprite_id;
    world.animations.set_frame_counts(entity, corundum::sprites::frame_counts_of(*idle));
    world.sprites.frame_index_ref(entity) = 0;
  }

  void dress_as_fighter(Fight &fight, EntityId entity) {
    dress_as(fight, entity, "fighter_idle");
  }

  /// Give @p entity a motion row with the fighter's ready sheet, as a spawned fighter would have.
  void give_stance(Fight &fight, EntityId entity) {
    auto &motion_sprites = fight.engine->scene.world.motion_sprites;
    const corundum::sprites::Frames *ready = fight.engine->characters.get_sprite("fighter_ready");
    REQUIRE(ready != nullptr);
    if (motion_sprites.has(entity))
      motion_sprites.remove(entity);
    motion_sprites.insert(entity, corundum::entities::MotionSpriteTable::Config{
                                      .walk_id = fight.engine->characters.get_sprite_id("player_walk"),
                                      .idle_id = fight.engine->characters.get_sprite_id("fighter_idle"),
                                      .ready_id = ready->sprite_id,
                                      .ready_counts = corundum::sprites::frame_counts_of(*ready),
                                      .ready_frame_duration = k_dt,
                                  });
  }

  [[nodiscard]] bool plays(const Fight &fight, EntityId entity, const std::string &sprite) {
    const auto &sprites = fight.engine->scene.world.sprites;
    return sprites.sprite_id[sprites.dense_index(entity)] == fight.engine->characters.get_sprite_id(sprite);
  }

  /// Step until @p entity plays @p sprite; capped, so one that never does fails the checks that follow.
  void step_until_playing(Fight &fight, EntityId entity, const std::string &sprite) {
    for (int i = 0; i < 40 && !plays(fight, entity, sprite); ++i)
      step(fight);
  }

  /// Run the clips for @p steps fixed steps, as the world step does; the tests' step() doesn't.
  void run_clips(Fight &fight, int steps) {
    auto &world = fight.engine->scene.world;
    for (int i = 0; i < steps; ++i) {
      corundum::animation::update_clips(world.sprites, world.transforms, world.animations, world.facings, world.clips,
                                        k_dt);
      corundum::entities::mark_finished_clips_for_deletion(world);
      corundum::entities::flush_deletions(world);
    }
  }

  /// Spawn another foe, actor @p id, on @p tile. It has no collision box, so only the fight's own
  /// rules keep it off a tile.
  EntityId add_foe(Fight &fight, const std::string &id, TileCoord tile) {
    corundum::entities::World &world = fight.engine->scene.world;
    const corundum::entities::Position position{
        .col = static_cast<float>(tile.col),
        .row = static_cast<float>(tile.row),
    };
    const corundum::entities::Velocity velocity{};
    const corundum::entities::Sprite sprite{};
    const std::optional<EntityId> spawned = corundum::entities::spawn(world, position, velocity, sprite);
    if (!spawned) {
      FAIL("the entity pool is full");
      return {};
    }
    world.actor_ids.insert(*spawned, id);
    return *spawned;
  }

  /// Let the hero outlast any of these fights: the flat rules subtract this flag from 2 Health.
  void toughen_hero(Fight &fight) {
    fight.engine->flags["test.hero.damage"] = -1000;
  }

  /// Make creature @p creature unable to act once the `test.felled` flag is set.
  void fell_on_flag(Fight &fight, std::string creature) {
    auto &rules = combat_rules(fight);
    rules.can_act = [original = rules.can_act,
                     creature = std::move(creature)](const corundum::gameplay::rules::CombatContext &context,
                                                     const corundum::gameplay::rules::Combatant &combatant) {
      if (combatant.creature == creature && context.flags.read("test.felled") > 0)
        return false;
      return original(context, combatant);
    };
  }

  /// One step, plus the velocity integration @p walkers get from world::update in the real loop.
  /// follow_path only sets a velocity, and step() runs the gameplay step alone.
  void step_walking(Fight &fight, std::initializer_list<EntityId> walkers) {
    step(fight);
    for (const EntityId walker : walkers)
      corundum::physics::integrate(fight.engine->scene.world.transforms, walker, k_dt);
  }

  /// The tile @p entity stands on.
  TileCoord standing_tile(const Fight &fight, EntityId entity) {
    const auto &transforms = fight.engine->scene.world.transforms;
    return corundum::world::tile_of(transforms.pos_col(entity), transforms.pos_row(entity));
  }

} // namespace

TEST_CASE("combat: start_encounter starts the fight once the dialogue closes") {
  const auto fight = make_fight({.col = 9, .row = 8});
  fight->engine->scene.ui.push(screens::Dialogue);
  fight->gameplay->pending_dialogue_events.push_back({.name = "start_encounter", .args = {"a00", "grunt"}});
  step(*fight);
  REQUIRE_FALSE(fight->gameplay->battle.has_value());
  fight->engine->scene.ui.pop();

  step(*fight);

  CHECK(fight->gameplay->battle.has_value());
  CHECK_EQ(fight->engine->scene.mode(), screens::Combat);
}

TEST_CASE("combat: start_encounter for an actor not in the area toasts and starts nothing") {
  const auto fight = make_fight({.col = 9, .row = 8});
  fight->gameplay->pending_dialogue_events.push_back({.name = "start_encounter", .args = {"nobody", "grunt"}});

  step(*fight);

  CHECK_FALSE(fight->gameplay->battle.has_value());
  CHECK_EQ(fight->engine->scene.mode(), corundum::world::GameMode::Exploring);
  CHECK_EQ(last_toast(*fight->engine),
           "The fight can't start: [gameplay] start_encounter: no actor 'nobody' in this area");
}

TEST_CASE("combat: a creature that can't join toasts and starts nothing") {
  const auto fight = make_fight({.col = 9, .row = 8});

  start_fight(*fight, "missing");

  CHECK_FALSE(fight->gameplay->battle.has_value());
  CHECK_EQ(last_toast(*fight->engine),
           "The fight can't start: [gameplay] 'missing' can't join the encounter: no creature 'missing'");
}

TEST_CASE("combat: a ruleset without combat ignores start_encounter") {
  const auto fight = make_fight({.col = 9, .row = 8});
  fight->gameplay->ruleset.combat.reset();

  start_fight(*fight);

  CHECK_FALSE(fight->gameplay->battle.has_value());
  CHECK_EQ(fight->engine->scene.mode(), corundum::world::GameMode::Exploring);
}

TEST_CASE("combat: start_encounter during a fight is ignored") {
  const auto fight = make_fight({.col = 9, .row = 8});
  start_fight(*fight);
  fight->gameplay->pending_dialogue_events.push_back({.name = "start_encounter", .args = {"a00", "other"}});

  step(*fight);

  CHECK_FALSE(fight->gameplay->pending_encounter.has_value());
  CHECK_EQ(encounter_of(*fight).spec(combat::Battle::k_hostile).name, "grunt");
}

TEST_CASE("combat: Pause pushes CombatPaused over Combat, and a second press pops it") {
  const auto fight = make_fight({.col = 9, .row = 8});
  start_fight(*fight);

  press(*fight, Action::Pause);
  REQUIRE_EQ(fight->engine->scene.mode(), screens::CombatPaused);
  press(*fight, Action::Pause);

  CHECK_EQ(fight->engine->scene.mode(), screens::Combat);
}

TEST_CASE("combat: the timeline holds while the fight is paused") {
  const auto fight = make_fight({.col = 9, .row = 8});
  start_fight(*fight);
  press(*fight, Action::Pause);
  step(*fight);
  step(*fight);
  REQUIRE_EQ(encounter_of(*fight).beats(combat::Battle::k_hostile), 0);

  press(*fight, Action::Pause);

  CHECK_EQ(encounter_of(*fight).beats(combat::Battle::k_hostile), 20);
}

TEST_CASE("combat: the protagonist starts the fight with a standing attack on the hostile") {
  const auto fight = make_fight({.col = 9, .row = 8});

  start_fight(*fight);

  CHECK_EQ(encounter_of(*fight).target_of(combat::Battle::k_protagonist),
           std::optional<std::size_t>{combat::Battle::k_hostile});
}

TEST_CASE("combat: a click on the hostile's tile renews an attack order a move dropped") {
  const auto fight = make_fight({.col = 9, .row = 8});
  start_fight(*fight);
  click(*fight, {.col = 8, .row = 10});
  REQUIRE_FALSE(encounter_of(*fight).target_of(combat::Battle::k_protagonist).has_value());

  click(*fight, {.col = 9, .row = 8});

  CHECK_EQ(encounter_of(*fight).target_of(combat::Battle::k_protagonist),
           std::optional<std::size_t>{combat::Battle::k_hostile});
}

TEST_CASE("combat: saving during a fight is refused") {
  const auto fight = make_fight({.col = 9, .row = 8});
  start_fight(*fight);

  const std::expected<void, std::string> saved = fight->gameplay->save_to_slot("slot_01");

  REQUIRE_FALSE(saved.has_value());
  CHECK_EQ(saved.error(), "can't save during a fight");
}

TEST_CASE("combat: a won fight closes the combat layers, removes the hostile and raises encounter_won") {
  const auto fight = make_fight({.col = 9, .row = 8});
  fight->engine->flags["test.hero.start"] = 100;
  start_fight(*fight);
  click(*fight, {.col = 9, .row = 8});

  step_until_over(*fight);

  REQUIRE_FALSE(fight->gameplay->battle.has_value());
  CHECK_EQ(fight->engine->scene.mode(), corundum::world::GameMode::Exploring);
  REQUIRE_EQ(fight->raised.size(), 1);
  CHECK_EQ(fight->raised[0].name, "encounter_won");
  CHECK_EQ(fight->raised[0].args, std::vector<std::string>{"a00"});
  // The hero lands at ticks 2 and 8; the grunt, from 0 beats, lands once, at tick 6.
  CHECK_EQ(corundum::world::visit_count(fight->engine->flags, "test.hero.damage"), 1);
  CHECK_FALSE(fight->engine->scene.world.entities.is_live(fight->foe));
}

TEST_CASE("combat: a lost fight raises encounter_lost and leaves the hostile") {
  const auto fight = make_fight({.col = 9, .row = 8});
  fight->engine->flags["test.hero.damage"] = 1;
  start_fight(*fight);

  step_until_over(*fight);

  REQUIRE_FALSE(fight->gameplay->battle.has_value());
  CHECK_EQ(fight->engine->scene.mode(), corundum::world::GameMode::Exploring);
  REQUIRE_EQ(fight->raised.size(), 1);
  CHECK_EQ(fight->raised[0].name, "encounter_lost");
  CHECK_EQ(fight->raised[0].args, std::vector<std::string>{"a00"});
  CHECK(fight->engine->scene.world.entities.is_live(fight->foe));
}

TEST_CASE("combat: reset_session_state drops the fight and a pending one") {
  const auto fight = make_fight({.col = 9, .row = 8});
  start_fight(*fight);
  fight->gameplay->pending_encounter =
      combat::EncounterRequest{.hostiles = {{.actor_id = "a00", .creature_id = "grunt"}}};

  fight->gameplay->reset_session_state();

  CHECK_FALSE(fight->gameplay->battle.has_value());
  CHECK_FALSE(fight->gameplay->pending_encounter.has_value());
}

TEST_CASE("combat: losing the Combat layer, as an area change does, drops the fight") {
  const auto fight = make_fight({.col = 9, .row = 8});
  start_fight(*fight);
  fight->engine->scene.ui.clear();

  step(*fight);

  CHECK_FALSE(fight->gameplay->battle.has_value());
  CHECK(fight->raised.empty());
}

TEST_CASE("combat: an attack order out of reach paths the protagonist to a tile next to the target") {
  const auto fight = make_fight({.col = 10, .row = 8});
  start_fight(*fight);

  click(*fight, {.col = 10, .row = 8});

  CHECK_EQ(fight->engine->scene.path, std::vector<TileCoord>{{.col = 9, .row = 8}});
}

TEST_CASE("combat: a hostile out of reach walks toward the protagonist") {
  const auto fight = make_fight({.col = 10, .row = 8});
  start_fight(*fight);

  step(*fight);

  const auto &transforms = fight->engine->scene.world.transforms;
  CHECK_LT(transforms.dc[transforms.dense_index(fight->foe)], 0.f);
}

TEST_CASE("combat: a hostile in reach stands") {
  const auto fight = make_fight({.col = 9, .row = 8});
  start_fight(*fight);

  step(*fight);

  const auto &transforms = fight->engine->scene.world.transforms;
  const auto slot = transforms.dense_index(fight->foe);
  CHECK_EQ(transforms.dc[slot], 0.f);
  CHECK_EQ(transforms.dr[slot], 0.f);
}

TEST_CASE("combat: a protagonist in reach stands its ground") {
  const auto fight = make_fight({.col = 9, .row = 8});
  start_fight(*fight);
  fight->engine->scene.path = {TileCoord{.col = 8, .row = 9}};

  click(*fight, {.col = 9, .row = 8});

  CHECK(fight->engine->scene.path.empty());
}

TEST_CASE("combat: a ground click while the clock runs drops the attack order") {
  const auto fight = make_fight({.col = 9, .row = 8});
  start_fight(*fight);
  click(*fight, {.col = 9, .row = 8});

  click(*fight, {.col = 8, .row = 10});

  CHECK_FALSE(encounter_of(*fight).target_of(combat::Battle::k_protagonist).has_value());
}

TEST_CASE("combat: a ground click while paused keeps the attack order") {
  const auto fight = make_fight({.col = 9, .row = 8});
  start_fight(*fight);
  click(*fight, {.col = 9, .row = 8});
  press(*fight, Action::Pause);

  click(*fight, {.col = 8, .row = 10});

  CHECK_EQ(encounter_of(*fight).target_of(combat::Battle::k_protagonist),
           std::optional<std::size_t>{combat::Battle::k_hostile});
}

TEST_CASE("combat: holding a move key cancels the protagonist's wind-up") {
  const auto fight = make_fight({.col = 9, .row = 8});
  fight->engine->flags["test.hero.start"] = 100;
  start_fight(*fight);
  click(*fight, {.col = 9, .row = 8});
  REQUIRE_EQ(encounter_of(*fight).phase(combat::Battle::k_protagonist), combat::Phase::WindUp);

  fight->engine->input_state.held.set(static_cast<std::size_t>(Action::MoveUp));
  step(*fight);

  // Cancelled to the return point (0), then this step's tick adds Tempo.
  CHECK_EQ(encounter_of(*fight).phase(combat::Battle::k_protagonist), combat::Phase::Ready);
  CHECK_EQ(encounter_of(*fight).beats(combat::Battle::k_protagonist), 20);
  CHECK_FALSE(encounter_of(*fight).target_of(combat::Battle::k_protagonist).has_value());
}

TEST_CASE("combat: 1 renews the attack order a move dropped") {
  const auto fight = make_fight({.col = 9, .row = 8});
  start_fight(*fight);
  click(*fight, {.col = 8, .row = 10});
  REQUIRE_FALSE(encounter_of(*fight).target_of(combat::Battle::k_protagonist).has_value());

  press(*fight, Action::CombatCommand1);

  CHECK_EQ(encounter_of(*fight).target_of(combat::Battle::k_protagonist),
           std::optional<std::size_t>{combat::Battle::k_hostile});
}

TEST_CASE("combat: 1 drops a queued command, so the standing attack comes next") {
  const auto fight = make_fight({.col = 9, .row = 8});
  start_fight(*fight);
  press(*fight, Action::CombatCommand2);
  REQUIRE(encounter_of(*fight).queued(combat::Battle::k_protagonist).has_value());

  press(*fight, Action::CombatCommand1);

  CHECK_FALSE(encounter_of(*fight).queued(combat::Battle::k_protagonist).has_value());
}

TEST_CASE("combat: a click on the hostile keeps a queued command") {
  const auto fight = make_fight({.col = 9, .row = 8});
  start_fight(*fight);
  press(*fight, Action::CombatCommand2);

  click(*fight, {.col = 9, .row = 8});

  CHECK_EQ(encounter_of(*fight).queued(combat::Battle::k_protagonist), std::optional<std::size_t>{0});
}

TEST_CASE("combat: 2 queues the command bar's second slot, the rules' first command") {
  const auto fight = make_fight({.col = 9, .row = 8});
  start_fight(*fight);

  press(*fight, Action::CombatCommand2);

  CHECK_EQ(encounter_of(*fight).queued(combat::Battle::k_protagonist), std::optional<std::size_t>{0});
}

TEST_CASE("combat: pressing a queued command's key again unqueues it") {
  const auto fight = make_fight({.col = 9, .row = 8});
  start_fight(*fight);
  press(*fight, Action::CombatCommand2);

  press(*fight, Action::CombatCommand2);

  CHECK_FALSE(encounter_of(*fight).queued(combat::Battle::k_protagonist).has_value());
}

TEST_CASE("combat: a command that isn't available is refused with a toast") {
  const auto fight = make_fight({.col = 9, .row = 8});
  start_fight(*fight);

  press(*fight, Action::CombatCommand3);

  CHECK_FALSE(encounter_of(*fight).queued(combat::Battle::k_protagonist).has_value());
  CHECK_EQ(last_toast(*fight->engine), "Tonic can't be used now");
}

TEST_CASE("combat: a command key queues while the fight is paused") {
  const auto fight = make_fight({.col = 9, .row = 8});
  start_fight(*fight);
  press(*fight, Action::Pause);

  press(*fight, Action::CombatCommand2);

  CHECK_EQ(encounter_of(*fight).queued(combat::Battle::k_protagonist), std::optional<std::size_t>{0});
  CHECK_EQ(fight->engine->scene.mode(), screens::CombatPaused);
}

TEST_CASE("combat: a command aimed at an enemy renews the attack a move dropped") {
  const auto fight = make_fight({.col = 9, .row = 8});
  start_fight(*fight);
  click(*fight, {.col = 8, .row = 10});

  press(*fight, Action::CombatCommand2);

  CHECK_EQ(encounter_of(*fight).target_of(combat::Battle::k_protagonist),
           std::optional<std::size_t>{combat::Battle::k_hostile});
}

TEST_CASE("combat: a queued tonic is drunk at the next command point") {
  const auto fight = make_fight({.col = 9, .row = 8});
  fight->engine->flags["test.hero.start"] = 90;
  fight->engine->flags["test.hero.tonics"] = 1;
  fight->engine->flags["test.hero.damage"] = 1;
  start_fight(*fight);
  press(*fight, Action::CombatCommand3);

  step(*fight);

  CHECK_EQ(corundum::world::visit_count(fight->engine->flags, "test.hero.tonics"), 0);
  CHECK_EQ(corundum::world::visit_count(fight->engine->flags, "test.hero.damage"), 0);
  CHECK_EQ(last_toast(*fight->engine), "hero drinks a tonic");
}

TEST_CASE("combat: a click on a tile the hostile's sprite is drawn over attacks it") {
  // The hostile stands on (9, 8); its sprite covers (8, 7), (8, 8) and (9, 7) as well.
  const auto fight = make_fight({.col = 9, .row = 8});
  start_fight(*fight);
  click(*fight, {.col = 8, .row = 10});

  click(*fight, {.col = 8, .row = 7});

  CHECK_EQ(encounter_of(*fight).target_of(combat::Battle::k_protagonist),
           std::optional<std::size_t>{combat::Battle::k_hostile});
}

TEST_CASE("combat: a click two tiles behind the hostile is a ground move") {
  const auto fight = make_fight({.col = 9, .row = 8});
  start_fight(*fight);

  click(*fight, {.col = 9, .row = 6});

  CHECK_FALSE(encounter_of(*fight).target_of(combat::Battle::k_protagonist).has_value());
}

TEST_CASE("combat: a heavy wind-up in reach pauses the fight and says why") {
  const auto fight = make_fight({.col = 9, .row = 8});
  start_fight(*fight, "brute");

  step_until_paused(*fight);

  CHECK_EQ(fight->engine->scene.mode(), screens::CombatPaused);
  CHECK_EQ(battle_of(*fight).pause_note(), "brute is winding up Smash");
}

TEST_CASE("combat: resuming an auto-paused fight clears the note") {
  const auto fight = make_fight({.col = 9, .row = 8});
  start_fight(*fight, "brute");
  step_until_paused(*fight);
  REQUIRE_EQ(fight->engine->scene.mode(), screens::CombatPaused);

  press(*fight, Action::Pause);

  CHECK_EQ(fight->engine->scene.mode(), screens::Combat);
  CHECK(battle_of(*fight).pause_note().empty());
}

TEST_CASE("combat: a standing wind-up doesn't pause the fight") {
  const auto fight = make_fight({.col = 9, .row = 8});
  start_fight(*fight);
  for (int i = 0; i < 5; ++i)
    step(*fight);
  REQUIRE_EQ(encounter_of(*fight).phase(combat::Battle::k_hostile), combat::Phase::WindUp);

  CHECK_EQ(fight->engine->scene.mode(), screens::Combat);
}

TEST_CASE("combat: the command bar lists Attack, the rules' commands, then their toggles") {
  const auto fight = make_fight({.col = 9, .row = 8});

  start_fight(*fight);

  const corundum::gameplay::screens::CombatHud &hud = battle_of(*fight).hud();
  REQUIRE_EQ(hud.commands.size(), 3);
  CHECK_EQ(hud.commands[0].label, "Attack");
  CHECK_EQ(hud.commands[0].action, Action::CombatCommand1);
  CHECK_EQ(hud.commands[1].label, "Bash");
  CHECK_EQ(hud.commands[1].action, Action::CombatCommand2);
  CHECK(hud.commands[1].is_available);
  CHECK_EQ(hud.commands[2].label, "Tonic");
  CHECK_EQ(hud.commands[2].action, Action::CombatCommand3);
  CHECK_FALSE(hud.commands[2].is_available);
  REQUIRE_EQ(hud.toggles.size(), 1);
  CHECK_EQ(hud.toggles[0].label, "Guard");
  CHECK_EQ(hud.toggles[0].action, Action::CombatCommand4);
  CHECK_EQ(hud.page.count, 2);
}

TEST_CASE("combat: the command bar marks the queued command") {
  const auto fight = make_fight({.col = 9, .row = 8});
  start_fight(*fight);

  press(*fight, Action::CombatCommand2);

  const std::vector<corundum::gameplay::screens::CombatHudCommand> &commands = battle_of(*fight).hud().commands;
  CHECK(commands[1].is_highlighted);
  CHECK_FALSE(commands[2].is_highlighted);
}

TEST_CASE("combat: the HUD names the command a hostile is winding up") {
  const auto fight = make_fight({.col = 9, .row = 8});
  start_fight(*fight, "brute");

  step_until_paused(*fight);

  const corundum::gameplay::screens::CombatHudRow &row = battle_of(*fight).hud().rows[combat::Battle::k_hostile];
  CHECK_EQ(row.windup_label, "Smash");
  CHECK_EQ(row.windup_end_beats, 130);
}

TEST_CASE("combat: 4 turns the bar's fourth slot, a toggle, on") {
  const auto fight = make_fight({.col = 9, .row = 8});
  start_fight(*fight);

  press(*fight, Action::CombatCommand4);

  CHECK_EQ(corundum::world::visit_count(fight->engine->flags, "test.hero.guard"), 1);
  CHECK(battle_of(*fight).hud().toggles[0].is_highlighted);
}

TEST_CASE("combat: pressing an on toggle's key turns it off") {
  const auto fight = make_fight({.col = 9, .row = 8});
  start_fight(*fight);
  press(*fight, Action::CombatCommand4);

  press(*fight, Action::CombatCommand4);

  CHECK_EQ(corundum::world::visit_count(fight->engine->flags, "test.hero.guard"), 0);
}

TEST_CASE("combat: a toggle works while the fight is paused") {
  const auto fight = make_fight({.col = 9, .row = 8});
  start_fight(*fight);
  press(*fight, Action::Pause);

  press(*fight, Action::CombatCommand4);

  CHECK_EQ(corundum::world::visit_count(fight->engine->flags, "test.hero.guard"), 1);
  CHECK_EQ(fight->engine->scene.mode(), screens::CombatPaused);
}

TEST_CASE("combat: an unavailable toggle is refused with a toast") {
  const auto fight = make_fight({.col = 9, .row = 8});
  combat_rules(*fight).toggles = [](const corundum::gameplay::rules::CombatContext &,
                                    const corundum::gameplay::rules::Combatant &) {
    return std::vector<corundum::gameplay::rules::ToggleOption>{
        {.id = "rally", .is_available = false, .is_on = false, .label = "Rally"},
    };
  };
  start_fight(*fight);

  press(*fight, Action::CombatCommand4);

  CHECK_EQ(last_toast(*fight->engine), "Rally can't be used now");
}

TEST_CASE("combat: gamepad R1 shows the next page on A, Y and B") {
  const auto fight = make_fight({.col = 9, .row = 8});
  start_fight(*fight);

  press_gamepad(*fight, corundum::input::GamepadControl::RightBumper);

  const corundum::gameplay::screens::CombatHud &hud = battle_of(*fight).hud();
  CHECK_EQ(hud.page.index, 1);
  CHECK_EQ(hud.toggles[0].gamepad_action, std::optional<Action>{Action::CombatCommand1});
  CHECK_FALSE(hud.commands[0].gamepad_action.has_value());
}

TEST_CASE("combat: R1 on the last page wraps to the first") {
  const auto fight = make_fight({.col = 9, .row = 8});
  start_fight(*fight);
  press_gamepad(*fight, corundum::input::GamepadControl::RightBumper);

  press_gamepad(*fight, corundum::input::GamepadControl::RightBumper);

  CHECK_EQ(battle_of(*fight).hud().page.index, 0);
}

TEST_CASE("combat: L1 on the first page wraps to the last") {
  const auto fight = make_fight({.col = 9, .row = 8});
  start_fight(*fight);

  press_gamepad(*fight, corundum::input::GamepadControl::LeftBumper);

  CHECK_EQ(battle_of(*fight).hud().page.index, 1);
}

TEST_CASE("combat: on a keyboard, 1 still attacks after a gamepad paged") {
  const auto fight = make_fight({.col = 9, .row = 8});
  start_fight(*fight);
  press_gamepad(*fight, corundum::input::GamepadControl::RightBumper);
  click(*fight, {.col = 8, .row = 10});
  fight->engine->input_mapper.begin_poll();
  fight->engine->input_mapper.key(corundum::input::Key::Digit1, true);
  fight->engine->input_mapper.key(corundum::input::Key::Digit1, false);
  fight->engine->input_mapper.end_poll(fight->engine->input_state);

  step(*fight);

  CHECK_EQ(encounter_of(*fight).target_of(combat::Battle::k_protagonist),
           std::optional<std::size_t>{combat::Battle::k_hostile});
  CHECK_EQ(corundum::world::visit_count(fight->engine->flags, "test.hero.guard"), 0);
}

TEST_CASE("combat: the HUD rows show each combatant's status labels") {
  const auto fight = make_fight({.col = 9, .row = 8});
  fight->engine->flags["test.hero.guard"] = 1;

  start_fight(*fight);

  const corundum::gameplay::screens::CombatHud &hud = battle_of(*fight).hud();
  CHECK_EQ(hud.rows[combat::Battle::k_protagonist].status_labels, std::vector<std::string>{"Guard"});
  CHECK(hud.rows[combat::Battle::k_hostile].status_labels.empty());
}

TEST_CASE("combat: a protagonist that can't move keeps its attack through a ground click and is held") {
  const auto fight = make_fight({.col = 9, .row = 8});
  fight->engine->flags["test.hero.rooted"] = 1;
  start_fight(*fight);

  click(*fight, {.col = 8, .row = 10});

  CHECK_EQ(encounter_of(*fight).target_of(combat::Battle::k_protagonist),
           std::optional<std::size_t>{combat::Battle::k_hostile});
  CHECK(fight->engine->scene.is_player_held);
  CHECK(fight->engine->scene.path.empty());
}

TEST_CASE("combat: a protagonist that can't move ignores held movement keys") {
  const auto fight = make_fight({.col = 9, .row = 8});
  fight->engine->flags["test.hero.rooted"] = 1;
  fight->engine->flags["test.hero.start"] = 100;
  start_fight(*fight);
  step(*fight);
  REQUIRE_EQ(encounter_of(*fight).phase(combat::Battle::k_protagonist), combat::Phase::WindUp);

  fight->engine->input_state.held.set(static_cast<std::size_t>(Action::MoveUp));
  step(*fight);

  CHECK_EQ(encounter_of(*fight).phase(combat::Battle::k_protagonist), combat::Phase::Ready);
  CHECK_EQ(encounter_of(*fight).target_of(combat::Battle::k_protagonist),
           std::optional<std::size_t>{combat::Battle::k_hostile});
}

TEST_CASE("combat: a protagonist that can move again isn't held") {
  const auto fight = make_fight({.col = 9, .row = 8});
  fight->engine->flags["test.hero.rooted"] = 1;
  start_fight(*fight);
  step(*fight);
  REQUIRE(fight->engine->scene.is_player_held);
  fight->engine->flags["test.hero.rooted"] = 0;

  step(*fight);

  CHECK_FALSE(fight->engine->scene.is_player_held);
}

TEST_CASE("combat: a hostile that can't move doesn't walk") {
  const auto fight = make_fight({.col = 10, .row = 8});
  start_fight(*fight, "statue");

  step(*fight);

  const auto &transforms = fight->engine->scene.world.transforms;
  CHECK_EQ(transforms.dc[transforms.dense_index(fight->foe)], 0.f);
}

TEST_CASE("combat: the end of a fight lets the player walk again") {
  const auto fight = make_fight({.col = 9, .row = 8});
  fight->engine->flags["test.hero.rooted"] = 1;
  fight->engine->flags["test.hero.damage"] = 1;
  start_fight(*fight);

  step_until_over(*fight);

  REQUIRE_FALSE(fight->gameplay->battle.has_value());
  CHECK_FALSE(fight->engine->scene.is_player_held);
}

TEST_CASE("combat: a protagonist that can't move is held from the step its fight starts") {
  const auto fight = make_fight({.col = 9, .row = 8});
  fight->engine->flags["test.hero.rooted"] = 1;

  start_fight(*fight);

  REQUIRE(fight->gameplay->battle.has_value());
  CHECK(fight->engine->scene.is_player_held);
}

TEST_CASE("combat: losing the Combat layer lets a held protagonist walk again") {
  const auto fight = make_fight({.col = 9, .row = 8});
  fight->engine->flags["test.hero.rooted"] = 1;
  start_fight(*fight);
  REQUIRE(fight->engine->scene.is_player_held);
  fight->engine->scene.ui.clear();

  step(*fight);

  REQUIRE_FALSE(fight->gameplay->battle.has_value());
  CHECK_FALSE(fight->engine->scene.is_player_held);
}

TEST_CASE("combat: reset_session_state lets a held protagonist walk again") {
  const auto fight = make_fight({.col = 9, .row = 8});
  fight->engine->flags["test.hero.rooted"] = 1;
  start_fight(*fight);
  REQUIRE(fight->engine->scene.is_player_held);

  fight->gameplay->reset_session_state();

  CHECK_FALSE(fight->engine->scene.is_player_held);
}

TEST_CASE("combat: on the second page, gamepad A presses the fourth slot and keeps a queued command") {
  const auto fight = make_fight({.col = 9, .row = 8});
  start_fight(*fight);
  press(*fight, Action::CombatCommand2);
  REQUIRE_EQ(encounter_of(*fight).queued(combat::Battle::k_protagonist), std::optional<std::size_t>{0});
  press_gamepad(*fight, corundum::input::GamepadControl::RightBumper);

  press_gamepad(*fight, corundum::input::GamepadControl::A);

  CHECK_EQ(corundum::world::visit_count(fight->engine->flags, "test.hero.guard"), 1);
  CHECK_EQ(encounter_of(*fight).queued(combat::Battle::k_protagonist), std::optional<std::size_t>{0});
}

TEST_CASE("combat: rules listing more slots than the bar holds can't start the fight") {
  const auto fight = make_fight({.col = 9, .row = 8});
  combat_rules(*fight).toggles = [](const corundum::gameplay::rules::CombatContext &,
                                    const corundum::gameplay::rules::Combatant &) {
    return std::vector<corundum::gameplay::rules::ToggleOption>(7, {.id = "rally", .label = "Rally"});
  };

  start_fight(*fight);

  CHECK_FALSE(fight->gameplay->battle.has_value());
  CHECK_EQ(last_toast(*fight->engine),
           "The fight can't start: [gameplay] the command bar holds 9 slots; the rules list 10");
}

TEST_CASE("combat: start_encounter with two pairs starts a fight with both hostiles") {
  const auto fight = make_fight({.col = 9, .row = 8});
  add_foe(*fight, "a01", {.col = 7, .row = 8});

  start_pair(*fight);

  REQUIRE_EQ(encounter_of(*fight).size(), 3u);
  CHECK_EQ(encounter_of(*fight).spec(1).name, "first");
  CHECK_EQ(encounter_of(*fight).spec(2).name, "second");
  CHECK_EQ(encounter_of(*fight).target_of(1), std::optional<std::size_t>{combat::Battle::k_protagonist});
  CHECK_EQ(encounter_of(*fight).target_of(2), std::optional<std::size_t>{combat::Battle::k_protagonist});
}

TEST_CASE("combat: start_encounter with an odd argument count toasts and starts nothing") {
  const auto fight = make_fight({.col = 9, .row = 8});

  start_with_args(*fight, {"a00", "first", "a01"});

  CHECK_FALSE(fight->gameplay->battle.has_value());
  CHECK_FALSE(fight->gameplay->pending_encounter.has_value());
  CHECK_EQ(fight->engine->scene.mode(), corundum::world::GameMode::Exploring);
  CHECK_EQ(last_toast(*fight->engine),
           "The fight can't start: [gameplay] start_encounter takes actor and creature pairs, not 3 arguments");
}

TEST_CASE("combat: a request naming no hostiles toasts and starts nothing") {
  const auto fight = make_fight({.col = 9, .row = 8});
  fight->gameplay->pending_encounter = combat::EncounterRequest{};

  step(*fight);

  CHECK_FALSE(fight->gameplay->battle.has_value());
  CHECK_EQ(last_toast(*fight->engine), "The fight can't start: [gameplay] start_encounter: it names no hostiles");
}

TEST_CASE("combat: start_encounter naming a second actor not in the area toasts and starts nothing") {
  const auto fight = make_fight({.col = 9, .row = 8});

  start_with_args(*fight, {"a00", "first", "nobody", "second"});

  CHECK_FALSE(fight->gameplay->battle.has_value());
  CHECK_EQ(last_toast(*fight->engine),
           "The fight can't start: [gameplay] start_encounter: no actor 'nobody' in this area");
}

TEST_CASE("combat: start_encounter naming one actor twice toasts and starts nothing") {
  const auto fight = make_fight({.col = 9, .row = 8});

  start_with_args(*fight, {"a00", "first", "a00", "second"});

  CHECK_FALSE(fight->gameplay->battle.has_value());
  CHECK_EQ(last_toast(*fight->engine), "The fight can't start: [gameplay] start_encounter: actor 'a00' is named twice");
}

TEST_CASE("combat: a fallen hostile leaves the scene while the fight goes on") {
  const auto fight = make_fight({.col = 9, .row = 8});
  const EntityId second = add_foe(*fight, "a01", {.col = 7, .row = 8});
  toughen_hero(*fight);
  fell_on_flag(*fight, "second");
  start_pair(*fight);
  fight->engine->flags["test.felled"] = 1;

  step(*fight);

  CHECK_FALSE(fight->engine->scene.world.entities.is_live(second));
  CHECK(fight->engine->scene.world.entities.is_live(fight->foe));
  CHECK(fight->gameplay->battle.has_value());
  CHECK_EQ(fight->engine->scene.mode(), screens::Combat);
}

TEST_CASE("combat: two hostiles heading for one free tile end on different tiles") {
  const auto fight = make_fight({.col = 12, .row = 8});
  const EntityId second = add_foe(*fight, "a01", {.col = 13, .row = 8});
  toughen_hero(*fight);
  start_pair(*fight);
  click(*fight, {.col = 8, .row = 8});
  const TileCoord hero_tile{.col = 8, .row = 8};

  const auto both_in_reach = [&] {
    return corundum::world::tile_distance(standing_tile(*fight, fight->foe), hero_tile) <= 1 &&
           corundum::world::tile_distance(standing_tile(*fight, second), hero_tile) <= 1;
  };
  for (int i = 0; i < 300 && !both_in_reach(); ++i)
    step_walking(*fight, {fight->foe, second});

  REQUIRE(both_in_reach());
  CHECK_NE(standing_tile(*fight, fight->foe), standing_tile(*fight, second));
}

TEST_CASE("combat: a hostile with every tile in reach claimed takes the first one that frees up") {
  const auto fight = make_fight({.col = 9, .row = 8});
  const std::vector<TileCoord> ring{
      {.col = 7, .row = 7}, {.col = 8, .row = 7}, {.col = 9, .row = 7}, {.col = 7, .row = 8},
      {.col = 7, .row = 9}, {.col = 8, .row = 9}, {.col = 9, .row = 9},
  };
  std::vector<std::string> args{"a00", "c0"};
  for (std::size_t i = 0; i < ring.size(); ++i) {
    const std::string actor = std::format("a{:02}", i + 1);
    add_foe(*fight, actor, ring[i]);
    args.push_back(actor);
    args.push_back(std::format("c{}", i + 1));
  }
  const EntityId last = add_foe(*fight, "a08", {.col = 8, .row = 12});
  args.emplace_back("a08");
  args.emplace_back("c8");
  toughen_hero(*fight);
  fell_on_flag(*fight, "c0");
  start_with_args(*fight, std::move(args));
  click(*fight, {.col = 5, .row = 5});
  fight->engine->flags["test.felled"] = 1;
  const TileCoord freed{.col = 9, .row = 8};

  for (int i = 0; i < 300 && standing_tile(*fight, last) != freed; ++i)
    step_walking(*fight, {last});

  CHECK(standing_tile(*fight, last) == freed);
}

TEST_CASE("combat: a fight starts on the nearest hostile") {
  const auto fight = make_fight({.col = 11, .row = 8});
  add_foe(*fight, "a01", {.col = 9, .row = 8});

  start_pair(*fight);

  CHECK_EQ(battle_of(*fight).target(), 2u);
  CHECK_EQ(encounter_of(*fight).target_of(combat::Battle::k_protagonist), std::optional<std::size_t>{2});
}

TEST_CASE("combat: hostiles equally near start the fight on the first named") {
  const auto fight = make_fight({.col = 9, .row = 8});
  add_foe(*fight, "a01", {.col = 7, .row = 8});

  start_pair(*fight);

  CHECK_EQ(battle_of(*fight).target(), 1u);
}

TEST_CASE("combat: a click on another hostile targets and attacks it") {
  const auto fight = make_fight({.col = 9, .row = 8});
  add_foe(*fight, "a01", {.col = 8, .row = 12});
  start_pair(*fight);
  REQUIRE_EQ(battle_of(*fight).target(), 1u);

  click(*fight, {.col = 8, .row = 12});

  CHECK_EQ(battle_of(*fight).target(), 2u);
  CHECK_EQ(encounter_of(*fight).target_of(combat::Battle::k_protagonist), std::optional<std::size_t>{2});
}

TEST_CASE("combat: a click on a hostile's tile beats the hostile whose sprite covers that tile") {
  const auto fight = make_fight({.col = 10, .row = 8});
  add_foe(*fight, "a01", {.col = 9, .row = 8});
  start_pair(*fight);
  click(*fight, {.col = 10, .row = 8});
  REQUIRE_EQ(battle_of(*fight).target(), 1u);

  click(*fight, {.col = 9, .row = 8});

  CHECK_EQ(battle_of(*fight).target(), 2u);
}

TEST_CASE("combat: the next-target key moves to the next hostile and wraps") {
  const auto fight = make_fight({.col = 9, .row = 8});
  add_foe(*fight, "a01", {.col = 7, .row = 8});
  start_pair(*fight);
  REQUIRE_EQ(battle_of(*fight).target(), 1u);

  press(*fight, Action::CombatTargetNext);
  REQUIRE_EQ(battle_of(*fight).target(), 2u);
  press(*fight, Action::CombatTargetNext);

  CHECK_EQ(battle_of(*fight).target(), 1u);
  CHECK_EQ(encounter_of(*fight).target_of(combat::Battle::k_protagonist), std::optional<std::size_t>{1});
}

TEST_CASE("combat: the next-target key works while the fight is paused") {
  const auto fight = make_fight({.col = 9, .row = 8});
  add_foe(*fight, "a01", {.col = 7, .row = 8});
  start_pair(*fight);
  press(*fight, Action::Pause);
  REQUIRE_EQ(fight->engine->scene.mode(), screens::CombatPaused);

  press(*fight, Action::CombatTargetNext);

  CHECK_EQ(battle_of(*fight).target(), 2u);
}

TEST_CASE("combat: the next-target key skips a hostile that has fallen") {
  const auto fight = make_fight({.col = 9, .row = 8});
  add_foe(*fight, "a01", {.col = 7, .row = 8});
  add_foe(*fight, "a02", {.col = 8, .row = 7});
  toughen_hero(*fight);
  fell_on_flag(*fight, "second");
  start_with_args(*fight, {"a00", "first", "a01", "second", "a02", "third"});
  REQUIRE_EQ(battle_of(*fight).target(), 1u);
  fight->engine->flags["test.felled"] = 1;
  step(*fight);

  press(*fight, Action::CombatTargetNext);

  CHECK_EQ(battle_of(*fight).target(), 3u);
}

TEST_CASE("combat: when the target falls the attack moves to the nearest hostile") {
  const auto fight = make_fight({.col = 9, .row = 8});
  add_foe(*fight, "a01", {.col = 12, .row = 8});
  add_foe(*fight, "a02", {.col = 8, .row = 10});
  toughen_hero(*fight);
  fell_on_flag(*fight, "first");
  start_with_args(*fight, {"a00", "first", "a01", "second", "a02", "third"});
  REQUIRE_EQ(battle_of(*fight).target(), 1u);
  fight->engine->flags["test.felled"] = 1;

  step(*fight);

  CHECK_EQ(battle_of(*fight).target(), 3u);
  CHECK_EQ(encounter_of(*fight).target_of(combat::Battle::k_protagonist), std::optional<std::size_t>{3});
}

TEST_CASE("combat: when the target falls a protagonist who walked off keeps walking") {
  const auto fight = make_fight({.col = 9, .row = 8});
  add_foe(*fight, "a01", {.col = 7, .row = 8});
  toughen_hero(*fight);
  fell_on_flag(*fight, "first");
  start_pair(*fight);
  click(*fight, {.col = 8, .row = 10});
  REQUIRE_FALSE(encounter_of(*fight).target_of(combat::Battle::k_protagonist).has_value());
  fight->engine->flags["test.felled"] = 1;

  step(*fight);

  CHECK_EQ(battle_of(*fight).target(), 2u);
  CHECK_FALSE(encounter_of(*fight).target_of(combat::Battle::k_protagonist).has_value());
}

TEST_CASE("combat: beating both hostiles wins, removes both and names the first actor") {
  const auto fight = make_fight({.col = 9, .row = 8});
  const EntityId second = add_foe(*fight, "a01", {.col = 7, .row = 8});
  toughen_hero(*fight);
  start_pair(*fight);

  step_until_over(*fight);

  CHECK_FALSE(fight->gameplay->battle.has_value());
  CHECK_FALSE(fight->engine->scene.world.entities.is_live(fight->foe));
  CHECK_FALSE(fight->engine->scene.world.entities.is_live(second));
  REQUIRE_EQ(fight->raised.size(), 1u);
  CHECK_EQ(fight->raised[0].name, "encounter_won");
  CHECK_EQ(fight->raised[0].args, std::vector<std::string>{"a00"});
}

TEST_CASE("combat: losing to two hostiles leaves both in the scene") {
  const auto fight = make_fight({.col = 9, .row = 8});
  const EntityId second = add_foe(*fight, "a01", {.col = 7, .row = 8});
  start_pair(*fight);

  step_until_over(*fight);

  CHECK(fight->engine->scene.world.entities.is_live(fight->foe));
  CHECK(fight->engine->scene.world.entities.is_live(second));
  REQUIRE_EQ(fight->raised.size(), 1u);
  CHECK_EQ(fight->raised[0].name, "encounter_lost");
  CHECK_EQ(fight->raised[0].args, std::vector<std::string>{"a00"});
}

TEST_CASE("combat: the HUD marks the target's row and follows the next-target key") {
  const auto fight = make_fight({.col = 9, .row = 8});
  add_foe(*fight, "a01", {.col = 7, .row = 8});
  start_pair(*fight);
  REQUIRE(battle_of(*fight).hud().rows[1].is_target);

  press(*fight, Action::CombatTargetNext);

  const auto &rows = battle_of(*fight).hud().rows;
  CHECK_FALSE(rows[0].is_target);
  CHECK_FALSE(rows[1].is_target);
  CHECK(rows[2].is_target);
}

TEST_CASE("combat: target_tile names the tile the target stands on") {
  const auto fight = make_fight({.col = 9, .row = 8});
  const TileCoord hostile_tile{.col = 7, .row = 8};
  add_foe(*fight, "a01", hostile_tile);
  start_pair(*fight);

  press(*fight, Action::CombatTargetNext);

  CHECK(battle_of(*fight).target_tile() == hostile_tile);
}

TEST_CASE("combat: the target marker outlines the target's tile through the world view") {
  const auto fight = make_fight({.col = 9, .row = 8});
  const TileCoord hostile_tile{.col = 7, .row = 8};
  add_foe(*fight, "a01", hostile_tile);
  start_pair(*fight);
  press(*fight, Action::CombatTargetNext);
  corundum::Engine &engine = *fight->engine;
  engine.render.world_view.camera = {.x = 3.f, .y = 4.f};
  const corundum::ScreenSpec *spec = engine.screens.find(screens::Combat);
  REQUIRE(spec != nullptr);
  corundum::test::RecordingRenderer r;

  spec->render(engine, r, {.x = 320.f, .y = 240.f});

  const corundum::world::MapView map = corundum::world::build_map_view(engine.render, engine.cfg);
  const corundum::world::TileDiamond expected = corundum::world::tile_diamond(
      hostile_tile, corundum::world::discrete_elevation_at(map, hostile_tile.col, hostile_tile.row),
      corundum::world::projection_of(map, engine.cfg));
  REQUIRE_EQ(r.world_lines.size(), 4u);
  CHECK_EQ(r.world_lines[0].start.x, expected.top.x);
  CHECK_EQ(r.world_lines[0].start.y, expected.top.y);
  CHECK_EQ(r.world_lines[0].end.x, expected.right.x);
  CHECK_EQ(r.world_lines[1].end.y, expected.bottom.y);
  CHECK_EQ(r.world_lines[2].end.x, expected.left.x);
  CHECK_EQ(r.world_lines[3].end.x, expected.top.x);
  CHECK_EQ(r.world_lines[3].end.y, expected.top.y);
  CHECK_EQ(r.world_view_camera.x, 3.f);
  CHECK_EQ(r.world_view_camera.y, 4.f);
  CHECK_FALSE(r.is_world_view);
}

TEST_CASE("combat: a wind-up plays the attack clip, facing the target, with its middle at the landing") {
  const auto fight = make_fight({.col = 9, .row = 8});
  const EntityId player = fight->engine->scene.player;
  dress_as_fighter(*fight, player);
  fight->engine->flags["test.hero.start"] = 100;
  start_fight(*fight);

  step_until_playing(*fight, player, "fighter_attack");

  const auto &world = fight->engine->scene.world;
  REQUIRE(plays(*fight, player, "fighter_attack"));
  CHECK_EQ(world.facings.dir_of(player), corundum::core::Direction::SouthEast);
  // One tick of wind-up at 1/60 s, doubled and spread over the sheet's 4 frames.
  CHECK(world.clips.frame_duration_of(player) == doctest::Approx(1.f / 120.f));
  CHECK_EQ(world.clips.end_of(player), corundum::entities::ClipEnd::Resume);
}

TEST_CASE("combat: a Self command plays no attack clip") {
  const auto fight = make_fight({.col = 9, .row = 8});
  const EntityId player = fight->engine->scene.player;
  dress_as_fighter(*fight, player);
  fight->engine->flags["test.hero.tonics"] = 1;
  start_fight(*fight);
  press(*fight, Action::CombatCommand3);

  for (int i = 0; i < 40 && encounter_of(*fight).windup_label(combat::Battle::k_protagonist) != "Tonic"; ++i)
    step(*fight);

  REQUIRE_EQ(encounter_of(*fight).windup_label(combat::Battle::k_protagonist), std::string_view{"Tonic"});
  CHECK_FALSE(plays(*fight, player, "fighter_attack"));
}

TEST_CASE("combat: a hit plays the target's hurt clip") {
  const auto fight = make_fight({.col = 9, .row = 8});
  dress_as_fighter(*fight, fight->foe);
  fight->engine->flags["test.hero.start"] = 100;
  start_fight(*fight);

  step_until_playing(*fight, fight->foe, "fighter_hurt");

  REQUIRE(plays(*fight, fight->foe, "fighter_hurt"));
  CHECK_EQ(battle_of(*fight).hud().rows[combat::Battle::k_hostile].health.current, 1);
}

TEST_CASE("combat: a hit doesn't interrupt a wind-up") {
  const auto fight = make_fight({.col = 9, .row = 8});
  const EntityId player = fight->engine->scene.player;
  dress_as_fighter(*fight, player);
  fight->engine->flags["test.hero.start"] = 100;
  combat_rules(*fight).tempo_beats = 10; // Two ticks of wind-up, so a step passes inside it.
  start_fight(*fight);
  step_until_playing(*fight, player, "fighter_attack");

  fight->engine->flags["test.hero.damage"] = 1;
  step(*fight);

  CHECK_EQ(encounter_of(*fight).phase(combat::Battle::k_protagonist), combat::Phase::WindUp);
  CHECK(plays(*fight, player, "fighter_attack"));
}

TEST_CASE("combat: a Broken wind-up gives way to the hurt clip") {
  const auto fight = make_fight({.col = 9, .row = 8});
  dress_as_fighter(*fight, fight->foe);
  fight->engine->flags["test.hero.start"] = 10;
  combat_rules(*fight).tempo_beats = 10;
  start_fight(*fight);
  // With this start and tempo, Bash lands one tick into the foe's two-tick wind-up; change them together.
  press(*fight, Action::CombatCommand2);
  step_until_playing(*fight, fight->foe, "fighter_attack");

  step_until_playing(*fight, fight->foe, "fighter_hurt");

  REQUIRE(plays(*fight, fight->foe, "fighter_hurt"));
  CHECK_EQ(encounter_of(*fight).phase(combat::Battle::k_hostile), combat::Phase::Ready);
}

TEST_CASE("combat: the hurt clip returns to the sheet the fighter was on") {
  const auto fight = make_fight({.col = 9, .row = 8});
  dress_as_fighter(*fight, fight->foe);
  fight->engine->flags["test.hero.start"] = 100;
  start_fight(*fight);
  step_until_playing(*fight, fight->foe, "fighter_hurt");

  run_clips(*fight, 2);

  CHECK(plays(*fight, fight->foe, "fighter_idle"));
  CHECK_FALSE(fight->engine->scene.world.clips.has(fight->foe));
}

TEST_CASE("combat: a fallen hostile with a death clip stays as a corpse until the clip ends") {
  const auto fight = make_fight({.col = 9, .row = 8});
  dress_as_fighter(*fight, fight->foe);
  fight->engine->flags["test.hero.start"] = 100;
  start_fight(*fight);

  step_until_over(*fight);

  auto &world = fight->engine->scene.world;
  REQUIRE_EQ(fight->raised.size(), 1);
  CHECK_EQ(fight->raised[0].name, "encounter_won");
  REQUIRE(world.entities.is_live(fight->foe));
  CHECK(plays(*fight, fight->foe, "fighter_die"));
  CHECK_FALSE(world.collisions.has(fight->foe));
  CHECK_FALSE(world.dialogue_refs.has(fight->foe));
  CHECK_FALSE(corundum::entities::find_actor(world, "a00").has_value());
  const auto slot = world.transforms.dense_index(fight->foe);
  CHECK_EQ(world.transforms.dc[slot], 0.f);
  CHECK_EQ(world.transforms.dr[slot], 0.f);

  run_clips(*fight, 2);
  CHECK(world.entities.is_live(fight->foe));

  run_clips(*fight, 1);
  CHECK_FALSE(world.entities.is_live(fight->foe));
}

TEST_CASE("combat: a lost fight waits for the protagonist's death clip") {
  const auto fight = make_fight({.col = 9, .row = 8});
  const EntityId player = fight->engine->scene.player;
  dress_as_fighter(*fight, player);
  fight->engine->flags["test.hero.damage"] = 1;
  start_fight(*fight);

  step_until_playing(*fight, player, "fighter_die");

  REQUIRE(plays(*fight, player, "fighter_die"));
  CHECK(fight->gameplay->battle.has_value());
  CHECK(fight->engine->scene.is_player_held);
  CHECK(fight->raised.empty());

  run_clips(*fight, 2);
  step(*fight);
  CHECK(fight->gameplay->battle.has_value());
  CHECK(fight->raised.empty());

  run_clips(*fight, 1);
  step(*fight);

  CHECK_FALSE(fight->gameplay->battle.has_value());
  REQUIRE_EQ(fight->raised.size(), 1);
  CHECK_EQ(fight->raised[0].name, "encounter_lost");
  CHECK(plays(*fight, player, "fighter_die"));
  const auto &sprites = fight->engine->scene.world.sprites;
  CHECK_EQ(sprites.frame_index[sprites.dense_index(player)], 2);
}

TEST_CASE("combat: orders are ignored while the protagonist falls") {
  const auto fight = make_fight({.col = 9, .row = 8});
  const EntityId player = fight->engine->scene.player;
  dress_as_fighter(*fight, player);
  add_foe(*fight, "a01", {.col = 7, .row = 8});
  fight->engine->flags["test.hero.damage"] = 1;
  start_pair(*fight);
  step_until_playing(*fight, player, "fighter_die");
  const std::size_t target_before = battle_of(*fight).target();

  press(*fight, Action::CombatTargetNext);

  CHECK_EQ(battle_of(*fight).target(), target_before);
}

TEST_CASE("combat: a lost fight doesn't wait for a hurt clip") {
  const auto fight = make_fight({.col = 9, .row = 8});
  const EntityId player = fight->engine->scene.player;
  dress_as(*fight, player, "wounded_idle");
  start_fight(*fight);
  fight->engine->flags["test.hero.damage"] = 1;
  step_until_playing(*fight, player, "wounded_hurt");
  REQUIRE(fight->engine->scene.world.clips.has(player));

  step_until_over(*fight);

  CHECK_FALSE(fight->gameplay->battle.has_value());
  REQUIRE_EQ(fight->raised.size(), 1);
  CHECK_EQ(fight->raised[0].name, "encounter_lost");
}

TEST_CASE("combat: a sheet that leaves a facing without frames plays no clip") {
  const auto fight = make_fight({.col = 9, .row = 8});
  const EntityId player = fight->engine->scene.player;
  dress_as(*fight, player, "partial_idle");
  fight->engine->flags["test.hero.start"] = 100;
  start_fight(*fight);

  step_until_over(*fight);

  CHECK_FALSE(plays(*fight, player, "partial_attack"));
  CHECK_FALSE(fight->engine->scene.world.clips.has(player));
}

TEST_CASE("combat: target_tile is empty when the target falls with the protagonist") {
  namespace rules = corundum::gameplay::rules;
  const auto fight = make_fight({.col = 9, .row = 8});
  const EntityId player = fight->engine->scene.player;
  dress_as_fighter(*fight, player);
  fight->engine->flags["test.hero.start"] = 100;
  int hero_turns = 0;
  // As poison can: the hero's second blow fells the foe, and the end of that turn fells the hero.
  combat_rules(*fight).end_turn = [&hero_turns](const rules::CombatContext &context, rules::Combatant &combatant,
                                                rules::TurnEnd) {
    if (!combatant.creature && ++hero_turns == 2)
      context.flag_writer.write("test.hero.damage", 2);
    return rules::TurnEndResult{};
  };
  start_fight(*fight);

  step_until_playing(*fight, player, "fighter_die");

  REQUIRE(fight->gameplay->battle.has_value());
  CHECK_FALSE(battle_of(*fight).target_tile().has_value());
}

TEST_CASE("combat: a fight puts its combatants in their stance") {
  const auto fight = make_fight({.col = 9, .row = 8});
  const EntityId player = fight->engine->scene.player;
  give_stance(*fight, player);
  give_stance(*fight, fight->foe);

  start_fight(*fight);

  const auto &motion_sprites = fight->engine->scene.world.motion_sprites;
  CHECK(motion_sprites.is_in_stance(player));
  CHECK(motion_sprites.is_in_stance(fight->foe));
}

TEST_CASE("combat: a combatant with no ready sheet fights without a stance") {
  const auto fight = make_fight({.col = 9, .row = 8});
  const EntityId player = fight->engine->scene.player;
  REQUIRE(fight->engine->scene.world.motion_sprites.has(player));

  start_fight(*fight);

  CHECK_FALSE(fight->engine->scene.world.motion_sprites.is_in_stance(player));
}

TEST_CASE("combat: a won fight stands the protagonist down") {
  const auto fight = make_fight({.col = 9, .row = 8});
  const EntityId player = fight->engine->scene.player;
  give_stance(*fight, player);
  fight->engine->flags["test.hero.start"] = 100;
  start_fight(*fight);

  step_until_over(*fight);

  REQUIRE_EQ(fight->raised.size(), 1);
  CHECK_EQ(fight->raised[0].name, "encounter_won");
  CHECK_FALSE(fight->engine->scene.world.motion_sprites.is_in_stance(player));
}

TEST_CASE("combat: a fight that loses a combatant from the scene stands the others down") {
  const auto fight = make_fight({.col = 9, .row = 8});
  const EntityId player = fight->engine->scene.player;
  give_stance(*fight, player);
  start_fight(*fight);

  corundum::entities::despawn(fight->engine->scene.world, fight->foe);
  step(*fight);

  CHECK_FALSE(fight->gameplay->battle.has_value());
  CHECK_FALSE(fight->engine->scene.world.motion_sprites.is_in_stance(player));
}
