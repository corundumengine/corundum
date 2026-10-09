// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/engine.hpp>
#include <corundum/gameplay/gameplay.hpp>
#include <corundum/gameplay/item/item.hpp>
#include <corundum/gameplay/item/registry.hpp>
#include <corundum/gameplay/screens/modes.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/input_intent.hpp>
#include <corundum/world/scene.hpp>
#include <corundum/world/ui_stack.hpp>

#include "world_transition_fixtures.hpp"

#include <cstddef>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

  using corundum::test::adopt_platform;
  using corundum::test::make_world_config;
  using corundum::world::GameMode;
  namespace screens = corundum::gameplay::screens;

  /// Route one physical action press through the engine's UI step, exactly as the fixed-step
  /// loop does. Inventory is an engine-owned hub tab, stepped by Engine, not world::update, so
  /// the shared advance_with() helper cannot drive it.
  bool press(corundum::Engine &engine, corundum::input::Action action) {
    corundum::input::InputState state{};
    state.pressed.set(static_cast<std::size_t>(action));
    engine.input_state = state;
    const auto intent = corundum::input::make_input_intent(engine.input_state, engine.input_mapper.last_device());
    return engine.update_engine_screens(intent);
  }

} // namespace

TEST_CASE("inventory — I toggles the panel and freezes the player, arrows move the cursor") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(engine.initialize(make_world_config(fixtures)).has_value());
  // NOLINTNEXTLINE(misc-const-correctness)
  corundum::gameplay::Gameplay gameplay{engine};
  REQUIRE(engine.scene.mode() == GameMode::Exploring);
  REQUIRE(gameplay.inventory_cursor == 0);

  // Three held items → three rows to wrap within.
  engine.flags["item.a"] = 1;
  engine.flags["item.b"] = 2;
  engine.flags["item.c"] = 1;

  // Press I: Exploring → Inventory, cursor reset.
  press(engine, corundum::input::Action::Inventory);
  CHECK(engine.scene.mode() == screens::Inventory);
  CHECK(gameplay.inventory_cursor == 0);

  // Arrows move the highlight while paused.
  press(engine, corundum::input::Action::MoveDown);
  CHECK(engine.scene.mode() == screens::Inventory);
  CHECK(gameplay.inventory_cursor == 1);
  press(engine, corundum::input::Action::MoveUp);
  CHECK(gameplay.inventory_cursor == 0);

  // Down past the last row wraps to the first (dialogue choice-list behaviour).
  press(engine, corundum::input::Action::MoveDown);
  press(engine, corundum::input::Action::MoveDown);
  press(engine, corundum::input::Action::MoveDown);
  CHECK(gameplay.inventory_cursor == 0);

  // Up past the first row wraps to the last.
  press(engine, corundum::input::Action::MoveUp);
  CHECK(gameplay.inventory_cursor == 2);

  // Press I again: Inventory → Exploring.
  press(engine, corundum::input::Action::Inventory);
  CHECK(engine.scene.mode() == GameMode::Exploring);

  // Esc also closes an open panel.
  press(engine, corundum::input::Action::Inventory);
  REQUIRE(engine.scene.mode() == screens::Inventory);
  press(engine, corundum::input::Action::Cancel);
  CHECK(engine.scene.mode() == GameMode::Exploring);

  // Opening again resets the cursor to the top.
  press(engine, corundum::input::Action::MoveDown);
  press(engine, corundum::input::Action::Inventory);
  CHECK(engine.scene.mode() == screens::Inventory);
  CHECK(gameplay.inventory_cursor == 0);

  engine.cleanup();
}

TEST_CASE("inventory — Activate equips the highlighted item and Activate again unequips it") {
  using corundum::gameplay::item::Item;
  using corundum::gameplay::item::ItemCategory;
  using corundum::gameplay::item::WeaponData;

  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(engine.initialize(make_world_config(fixtures)).has_value());
  // NOLINTNEXTLINE(misc-const-correctness)
  corundum::gameplay::Gameplay gameplay{engine};
  gameplay.items.add(
      Item{.category = ItemCategory::Weapon, .id = "sword", .name = "Sword", .weapon = WeaponData{.damage = 3}});
  engine.flags["item.sword"] = 1;

  press(engine, corundum::input::Action::Inventory);
  REQUIRE(engine.scene.mode() == screens::Inventory);
  REQUIRE(gameplay.inventory_lines.size() == 1);

  // Equip: the slot flag is set and the cached equipment column rebuilt.
  press(engine, corundum::input::Action::Activate);
  CHECK(engine.flags["equip.weapon.sword"] == 1);
  REQUIRE(gameplay.inventory_equipment.size() == 1);
  CHECK(gameplay.inventory_equipment[0].slot == "weapon");
  CHECK(gameplay.inventory_equipment[0].item_name == "Sword");

  // The character sheet shows the equipped item when opened.
  press(engine, corundum::input::Action::Cancel);
  press(engine, corundum::input::Action::Character);
  REQUIRE(engine.scene.mode() == screens::Character);
  REQUIRE(gameplay.character_sheet_info.equipment.size() == 1);
  CHECK(gameplay.character_sheet_info.equipment[0].slot == "weapon");
  CHECK(gameplay.character_sheet_info.equipment[0].item_name == "Sword");

  // Unequip: the flag is cleared and the slot empties on the next inventory rebuild.
  press(engine, corundum::input::Action::Cancel);
  press(engine, corundum::input::Action::Inventory);
  REQUIRE(engine.scene.mode() == screens::Inventory);
  press(engine, corundum::input::Action::Activate);
  CHECK_FALSE(corundum::world::has_flag(engine.flags, "equip.weapon.sword"));
  REQUIRE(gameplay.inventory_equipment.size() == 1);
  CHECK(gameplay.inventory_equipment[0].item_name.empty());

  engine.cleanup();
}

TEST_CASE("inventory — rows are built once on open and not rebuilt while it stays open") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(engine.initialize(make_world_config(fixtures)).has_value());
  // NOLINTNEXTLINE(misc-const-correctness)
  corundum::gameplay::Gameplay gameplay{engine};

  engine.flags["item.a"] = 1;
  engine.flags["item.b"] = 1;

  press(engine, corundum::input::Action::Inventory);
  REQUIRE(engine.scene.mode() == screens::Inventory);
  REQUIRE(gameplay.inventory_lines.size() == 2);

  // Run two real fixed steps with the tab open. The cached vector must be the same object
  // (same address) after stepping, not merely the same size: the step path never rebuilds it.
  const auto *const cached_lines = &gameplay.inventory_lines;
  engine.timer.accumulator = 2.f * engine.timer.target_dt;
  REQUIRE(engine.run_frame());
  CHECK(&gameplay.inventory_lines == cached_lines);

  // The inventory is read-only while open: a flag mutated from outside must not change the
  // cached rows until the tab is reopened (the whole point of building once on open).
  engine.flags["item.c"] = 1;
  press(engine, corundum::input::Action::MoveDown);
  CHECK(gameplay.inventory_lines.size() == 2);

  // Reopening rebuilds from the current flags.
  press(engine, corundum::input::Action::Cancel);
  press(engine, corundum::input::Action::Inventory);
  CHECK(gameplay.inventory_lines.size() == 3);

  engine.cleanup();
}
