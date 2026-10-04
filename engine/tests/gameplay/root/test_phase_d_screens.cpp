// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/engine.hpp>
#include <corundum/gameplay/codex/codex.hpp>
#include <corundum/gameplay/gameplay.hpp>
#include <corundum/gameplay/item/container.hpp>
#include <corundum/gameplay/item/item.hpp>
#include <corundum/gameplay/location/location.hpp>
#include <corundum/gameplay/screens/hud_strip.hpp>
#include <corundum/gameplay/screens/modes.hpp>
#include <corundum/gameplay/shop/shop.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/input_intent.hpp>
#include <corundum/world/flags.hpp>
#include <corundum/world/ui_stack.hpp>

#include "world_transition_fixtures.hpp"

#include <cstddef>
#include <filesystem>
#include <string>

namespace {

  using corundum::test::adopt_platform;
  using corundum::test::make_world_config;
  using corundum::world::GameMode;
  namespace screens = corundum::gameplay::screens;

  bool press(corundum::Engine &engine, corundum::input::Action action) {
    corundum::input::InputState state{};
    state.pressed.set(static_cast<std::size_t>(action));
    engine.input_state = state;
    const auto intent = corundum::input::make_input_intent(engine.input_state, engine.input_mapper.last_device());
    return engine.update_engine_screens(intent);
  }

  void init_engine(corundum::Engine &engine) {
    adopt_platform(engine, 320, 240);
    const std::filesystem::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
    REQUIRE(engine.initialize(make_world_config(fixtures)).has_value());
  }

} // namespace

TEST_CASE("codex: the Codex action opens the screen and Cancel closes it") {
  corundum::Engine engine{};
  init_engine(engine);
  corundum::gameplay::Gameplay gameplay{engine};
  gameplay.codex.add(corundum::gameplay::codex::CodexEntry{.id = "village", .title = "Greyhollow"});
  corundum::world::set_flag(engine.flags, corundum::gameplay::codex::flag_key("village"));

  REQUIRE(engine.scene.mode() == GameMode::Exploring);
  CHECK(press(engine, corundum::input::Action::Codex));
  CHECK(engine.scene.mode() == screens::Codex);
  REQUIRE(gameplay.codex_screen.entries.size() == 1);
  CHECK(gameplay.codex_screen.entries[0].title == "Greyhollow");

  press(engine, corundum::input::Action::Cancel);
  CHECK(engine.scene.mode() == GameMode::Exploring);

  engine.cleanup();
}

TEST_CASE("map: Activate on a discovered world location arms a return-to-world transition") {
  corundum::Engine engine{};
  init_engine(engine);
  corundum::gameplay::Gameplay gameplay{engine};
  gameplay.locations.add(corundum::gameplay::location::Location{
      .col = 40.f,
      .id = "village",
      .name = "Greyhollow",
      .return_to_world = true,
      .row = 32.f,
      .zone = "village",
  });
  corundum::world::set_flag(engine.flags, corundum::gameplay::location::discovery_flag_key("village"));

  press(engine, corundum::input::Action::Map);
  REQUIRE(engine.scene.mode() == screens::Map);

  press(engine, corundum::input::Action::Activate);
  CHECK(engine.scene.mode() == GameMode::Exploring);
  REQUIRE(engine.scene.pending_transition.has_value());
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access): REQUIRE above aborts if unset.
  CHECK(engine.scene.pending_transition->return_to_world);
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access): REQUIRE above aborts if unset.
  CHECK(engine.scene.pending_transition->spawn_col == 40);

  engine.cleanup();
}

TEST_CASE("loot: Activate moves one unit from the container to the player") {
  corundum::Engine engine{};
  init_engine(engine);
  corundum::gameplay::Gameplay gameplay{engine};
  gameplay.items.add(corundum::gameplay::item::Item{.id = "salt", .name = "Salt", .price = 10});
  gameplay.active_container_id = "chest";
  engine.flags[std::string{corundum::gameplay::item::container_item_flag_key("chest", "salt")}] = 2;
  engine.scene.ui.push(screens::Loot);

  REQUIRE(engine.scene.mode() == screens::Loot);
  press(engine, corundum::input::Action::Activate);
  CHECK(corundum::world::visit_count(engine.flags, std::string{"container.chest.item.salt"}) == 1);
  CHECK(corundum::world::visit_count(engine.flags, std::string{"item.salt"}) == 1);

  // Switching panes and activating returns the unit.
  press(engine, corundum::input::Action::MoveRight);
  press(engine, corundum::input::Action::Activate);
  CHECK(corundum::world::visit_count(engine.flags, std::string{"container.chest.item.salt"}) == 2);
  CHECK(corundum::world::visit_count(engine.flags, std::string{"item.salt"}) == 0);

  engine.cleanup();
}

TEST_CASE("barter: buying spends gold and grants the item") {
  corundum::Engine engine{};
  init_engine(engine);
  corundum::gameplay::Gameplay gameplay{engine};
  gameplay.items.add(corundum::gameplay::item::Item{.id = "salt", .name = "Salt", .price = 10});
  gameplay.shops.add(corundum::gameplay::shop::Shop{
      .id = "corvin",
      .name = "Corvin's Salt",
      .stock =
          {
              corundum::gameplay::shop::StockEntry{
                  .item = "salt",
                  .price = 5,
              },
          },
  });
  gameplay.active_shop_id = "corvin";
  engine.scene.ui.push(screens::Barter);
  engine.flags[std::string{corundum::gameplay::screens::k_gold_flag}] = 20;

  REQUIRE(engine.scene.mode() == screens::Barter);
  press(engine, corundum::input::Action::Activate);
  CHECK(corundum::world::visit_count(engine.flags, std::string{"item.salt"}) == 1);
  CHECK(corundum::world::visit_count(engine.flags, std::string{corundum::gameplay::screens::k_gold_flag}) == 15);

  // Switch to Sell and trade it back for half its base price (buy_rate defaults to 0.5).
  press(engine, corundum::input::Action::TabNext);
  press(engine, corundum::input::Action::Activate);
  CHECK(corundum::world::visit_count(engine.flags, std::string{"item.salt"}) == 0);
  CHECK(corundum::world::visit_count(engine.flags, std::string{corundum::gameplay::screens::k_gold_flag}) == 20);

  engine.cleanup();
}
