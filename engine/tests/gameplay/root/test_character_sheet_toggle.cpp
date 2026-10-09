// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/engine.hpp>
#include <corundum/gameplay/gameplay.hpp>
#include <corundum/gameplay/screens/character_sheet.hpp>
#include <corundum/gameplay/screens/modes.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/input_intent.hpp>
#include <corundum/world/scene.hpp>
#include <corundum/world/ui_stack.hpp>

#include "world_transition_fixtures.hpp"

#include <cstddef>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

  using corundum::test::adopt_platform;
  using corundum::test::make_world_config;
  using corundum::world::GameMode;
  namespace screens = corundum::gameplay::screens;

  /// Route one physical action press through the engine's UI step, exactly as the fixed-step
  /// loop does. The character sheet is stepped by Engine, not world::update, so the shared
  /// world helper cannot drive it.
  bool press(corundum::Engine &engine, corundum::input::Action action) {
    corundum::input::InputState state{};
    state.pressed.set(static_cast<std::size_t>(action));
    engine.input_state = state;
    const auto intent = corundum::input::make_input_intent(engine.input_state, engine.input_mapper.last_device());
    return engine.update_engine_screens(intent);
  }

} // namespace

TEST_CASE("character sheet — P toggles the sheet, values are sampled on open, Esc closes") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(engine.initialize(make_world_config(fixtures)).has_value());
  // NOLINTNEXTLINE(misc-const-correctness)
  corundum::gameplay::Gameplay gameplay{engine};
  REQUIRE(engine.scene.mode() == GameMode::Exploring);

  engine.flags[std::string{screens::k_experience_flag}] = 10;

  // P opens the sheet and samples the current XP.
  press(engine, corundum::input::Action::Character);
  CHECK(engine.scene.mode() == screens::Character);
  CHECK(gameplay.character_sheet_info.experience == 10);

  // The values are sampled on open: a flag change while open does not retint the panel.
  engine.flags[std::string{screens::k_experience_flag}] = 99;
  CHECK(gameplay.character_sheet_info.experience == 10);

  // P again closes it.
  press(engine, corundum::input::Action::Character);
  CHECK(engine.scene.mode() == GameMode::Exploring);

  // Reopening samples the updated flags.
  press(engine, corundum::input::Action::Character);
  REQUIRE(engine.scene.mode() == screens::Character);
  CHECK(gameplay.character_sheet_info.experience == 99);

  // Esc closes an open sheet.
  press(engine, corundum::input::Action::Cancel);
  CHECK(engine.scene.mode() == GameMode::Exploring);

  engine.cleanup();
}

TEST_CASE("character sheet — the hotkey is ignored while another screen is open") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  REQUIRE(engine.initialize(make_world_config(fixtures)).has_value());
  // NOLINTNEXTLINE(misc-const-correctness)
  corundum::gameplay::Gameplay gameplay{engine};

  press(engine, corundum::input::Action::Inventory);
  REQUIRE(engine.scene.mode() == screens::Inventory);

  press(engine, corundum::input::Action::Character);
  CHECK(engine.scene.mode() == screens::Inventory);

  engine.cleanup();
}
