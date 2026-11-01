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
#include <corundum/gameplay/screens/modes.hpp>
#include <corundum/input/actions.hpp>
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

  const combat::Encounter &encounter_of(const Fight &fight) {
    if (!fight.gameplay->battle)
      throw std::runtime_error("no fight is running");
    return fight.gameplay->battle->encounter();
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

TEST_CASE("combat: a click on the hostile's tile orders the protagonist's attack") {
  const auto fight = make_fight({.col = 9, .row = 8});
  start_fight(*fight);

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
