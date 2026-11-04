// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/engine.hpp>
#include <corundum/entities/entity.hpp>
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
#include <corundum/world/flags.hpp>
#include <corundum/world/picking.hpp>
#include <corundum/world/ui_stack.hpp>

#include "gameplay/rules/flat_ruleset.hpp"
#include "world_transition_fixtures.hpp"

#include <cstddef>
#include <expected>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
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

  /// Ask for a fight with a00 as @p creature, as a dialogue action does, and run the step that starts it.
  void start_fight(Fight &fight, std::string creature = "grunt") {
    fight.gameplay->pending_dialogue_events.push_back(
        {.name = "start_encounter", .args = {"a00", std::move(creature)}});
    step(fight);
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
  corundum::entities::flush_deletions(fight->engine->scene.world);
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
  fight->gameplay->pending_encounter = combat::EncounterRequest{.actor_id = "a00", .creature_id = "grunt"};

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
