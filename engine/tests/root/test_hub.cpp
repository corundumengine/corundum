// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/engine.hpp>
#include <corundum/gameplay/gameplay.hpp>
#include <corundum/gameplay/screens/modes.hpp>
#include <corundum/gameplay/shop/shop.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/bindings.hpp>
#include <corundum/input/input_intent.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/render/render_state.hpp>
#include <corundum/world/scene.hpp>
#include <corundum/world/ui_stack.hpp>

#include "world_transition_fixtures.hpp"

// json_fwd.hpp is include-cleaner's provider for nlohmann::json; json.hpp is still
// required to construct json values (the forward header is incomplete).
#include <nlohmann/json.hpp> // NOLINT(misc-include-cleaner): constructors live in json.hpp
#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <filesystem>
#include <string>

namespace {

  using corundum::test::adopt_platform;
  using corundum::test::make_world_config;
  using corundum::world::GameMode;
  namespace screens = corundum::gameplay::screens;

  /// Route one physical action press through the engine's UI step, exactly as the fixed-step
  /// loop does. The hub tabs are engine-owned, so the shared advance_with() helper cannot drive
  /// them.
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

TEST_CASE("hub: each hotkey opens its own tab and the same hotkey closes it") {
  corundum::Engine engine{};
  init_engine(engine);
  // NOLINTNEXTLINE(misc-const-correctness)
  corundum::gameplay::Gameplay gameplay{engine};
  REQUIRE(engine.scene.mode() == GameMode::Exploring);

  CHECK(press(engine, corundum::input::Action::Inventory));
  CHECK(engine.scene.mode() == screens::Inventory);

  // A different hotkey switches tabs in one step.
  CHECK(press(engine, corundum::input::Action::Codex));
  CHECK(engine.scene.mode() == screens::Codex);

  // The active tab's own hotkey closes the hub.
  CHECK(press(engine, corundum::input::Action::Codex));
  CHECK(engine.scene.mode() == GameMode::Exploring);

  CHECK(press(engine, corundum::input::Action::Journal));
  CHECK(engine.scene.mode() == screens::Journal);
  CHECK(press(engine, corundum::input::Action::Map));
  CHECK(engine.scene.mode() == screens::Map);
  CHECK(press(engine, corundum::input::Action::Map));
  CHECK(engine.scene.mode() == GameMode::Exploring);

  engine.cleanup();
}

TEST_CASE("hub: the Hub button opens the last tab and closes when one is on top") {
  corundum::Engine engine{};
  init_engine(engine);
  // NOLINTNEXTLINE(misc-const-correctness)
  corundum::gameplay::Gameplay gameplay{engine};

  // No tab opened yet: Hub opens the default, Inventory.
  CHECK(press(engine, corundum::input::Action::Hub));
  CHECK(engine.scene.mode() == screens::Inventory);
  CHECK(engine.scene.last_hub_mode == screens::Inventory);

  // Hub again closes the hub.
  CHECK(press(engine, corundum::input::Action::Hub));
  CHECK(engine.scene.mode() == GameMode::Exploring);

  // Opening a different tab updates the remembered tab; Hub reopens that one.
  press(engine, corundum::input::Action::Journal);
  press(engine, corundum::input::Action::Cancel);
  CHECK(press(engine, corundum::input::Action::Hub));
  CHECK(engine.scene.mode() == screens::Journal);

  engine.cleanup();
}

TEST_CASE("hub: TabNext/TabPrev cycle the four tabs with wrap-around") {
  corundum::Engine engine{};
  init_engine(engine);
  // NOLINTNEXTLINE(misc-const-correctness)
  corundum::gameplay::Gameplay gameplay{engine};
  press(engine, corundum::input::Action::Inventory);
  REQUIRE(engine.scene.mode() == screens::Inventory);

  press(engine, corundum::input::Action::TabNext);
  CHECK(engine.scene.mode() == screens::Journal);
  press(engine, corundum::input::Action::TabNext);
  CHECK(engine.scene.mode() == screens::Codex);
  press(engine, corundum::input::Action::TabNext);
  CHECK(engine.scene.mode() == screens::Map);
  // Wrap forward back to the first tab.
  press(engine, corundum::input::Action::TabNext);
  CHECK(engine.scene.mode() == screens::Inventory);
  // Wrap backward to the last tab.
  press(engine, corundum::input::Action::TabPrev);
  CHECK(engine.scene.mode() == screens::Map);
  CHECK(engine.scene.last_hub_mode == screens::Map);

  engine.cleanup();
}

TEST_CASE("hub: Cancel closes whichever tab is on top") {
  corundum::Engine engine{};
  init_engine(engine);
  // NOLINTNEXTLINE(misc-const-correctness)
  corundum::gameplay::Gameplay gameplay{engine};

  for (const corundum::input::Action open : {
           corundum::input::Action::Inventory,
           corundum::input::Action::Journal,
           corundum::input::Action::Codex,
           corundum::input::Action::Map,
       }) {
    press(engine, open);
    REQUIRE(engine.scene.mode() != GameMode::Exploring);
    press(engine, corundum::input::Action::Cancel);
    CHECK(engine.scene.mode() == GameMode::Exploring);
  }

  engine.cleanup();
}

TEST_CASE("hub: does not open over dialogue, prompt, menu, loot or barter") {
  corundum::Engine engine{};
  init_engine(engine);
  corundum::gameplay::Gameplay gameplay{engine};
  // Loot and barter close themselves when their target is unset, so give them a target.
  gameplay.active_container_id = "chest";
  gameplay.shops.add(corundum::gameplay::shop::Shop{.id = "shop", .name = "Shop"});
  gameplay.active_shop_id = "shop";

  for (const GameMode screen : {
           screens::Dialogue,
           GameMode::Prompt,
           GameMode::Menu,
           screens::Loot,
           screens::Barter,
           GameMode::Settings,
       }) {
    engine.scene.ui.clear();
    engine.scene.ui.push(screen);

    // Neither the Hub button nor a hotkey may open a tab over another screen.
    press(engine, corundum::input::Action::Hub);
    CHECK(engine.scene.mode() == screen);
    press(engine, corundum::input::Action::Inventory);
    CHECK(engine.scene.mode() == screen);
  }

  engine.cleanup();
}

TEST_CASE("hub: opens in World mode with no resident chunks") {
  corundum::Engine engine{};
  init_engine(engine);
  // NOLINTNEXTLINE(misc-const-correctness)
  corundum::gameplay::Gameplay gameplay{engine};
  REQUIRE(engine.render.mode == corundum::render::RenderMode::World);
  engine.render.chunks.clear();
  REQUIRE(engine.render.chunks.active_empty());

  // Engine-owned tabs do not depend on world::update, so an empty chunk window cannot strand
  // them (the old world::update dispatch was gated on chunks being resident).
  press(engine, corundum::input::Action::Inventory);
  CHECK(engine.scene.mode() == screens::Inventory);
  press(engine, corundum::input::Action::Cancel);
  press(engine, corundum::input::Action::Journal);
  CHECK(engine.scene.mode() == screens::Journal);

  engine.cleanup();
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): CHECK expands to branches.
TEST_CASE("hub: a pre-hub binding with gamepad Y on Journal still opens the Journal tab") {
  corundum::Engine engine{};
  init_engine(engine);
  // NOLINTNEXTLINE(misc-const-correctness)
  corundum::gameplay::Gameplay gameplay{engine};

  // Simulate a settings file saved before the hub existed: no Hub row, and Journal still owns
  // gamepad Y. parse_bindings() must leave that in place rather than stealing Y for Hub.
  nlohmann::json array = nlohmann::json::array();
  for (const nlohmann::json &row : corundum::input::serialize(corundum::input::default_bindings())) {
    if (row.at("action").get<std::string>() != "Hub")
      array.push_back(row);
  }
  array.push_back({{"action", "Journal"}, {"device", "gamepad"}, {"input", "Y"}});

  const auto bindings = corundum::input::parse_bindings(array, corundum::input::default_bindings());
  REQUIRE(bindings.has_value());
  REQUIRE(engine.input_mapper.set_bindings(*bindings).has_value());

  corundum::input::GamepadState pad{};
  pad.buttons[static_cast<std::size_t>(corundum::input::GamepadControl::Y)] = true;
  corundum::input::clear_pressed(engine.input_state);
  engine.input_mapper.begin_poll();
  engine.input_mapper.gamepad(pad);
  engine.input_mapper.end_poll(engine.input_state);
  REQUIRE(engine.input_state.is_pressed(corundum::input::Action::Journal));

  const auto intent = corundum::input::make_input_intent(engine.input_state, engine.input_mapper.last_device());
  CHECK(engine.update_engine_screens(intent));
  CHECK(engine.scene.mode() == screens::Journal);

  engine.cleanup();
}
