// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/core/window_mode.hpp>
#include <corundum/engine.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/bindings.hpp>
#include <corundum/input/input_intent.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/render/render_state.hpp>
#include <corundum/ui/settings.hpp>
#include <corundum/world/scene.hpp>
#include <corundum/world/ui_stack.hpp>

#include "world_transition_fixtures.hpp"

// json_fwd.hpp is include-cleaner's provider for nlohmann::json; json.hpp is still
// required to construct json values (the forward header is incomplete).
#include <nlohmann/json.hpp> // NOLINT(misc-include-cleaner): constructors live in json.hpp
#include <nlohmann/json_fwd.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace {

  using corundum::test::adopt_platform;
  using corundum::test::make_world_config;
  using corundum::world::GameMode;

  /// Route one physical action press through the engine's UI step, exactly as the fixed-step
  /// loop does. The Menu/Settings screens are stepped by Engine, not world::update, so the
  /// shared advance_with() helper (which only calls world::update) cannot drive them.
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

  /// Open Menu → Settings on the General tab and return the engine.
  void open_settings(corundum::Engine &engine) {
    press(engine, corundum::input::Action::Menu);
    press(engine, corundum::input::Action::MoveDown); // Resume → Settings
    press(engine, corundum::input::Action::Select);
  }

} // namespace

TEST_CASE("pause menu: Menu opens it, Menu and Cancel both close it") {
  corundum::Engine engine{};
  init_engine(engine);
  REQUIRE(engine.scene.mode() == GameMode::Exploring);

  CHECK(press(engine, corundum::input::Action::Menu));
  CHECK(engine.scene.mode() == GameMode::Menu);

  // Pressing Menu again closes it.
  CHECK(press(engine, corundum::input::Action::Menu));
  CHECK(engine.scene.mode() == GameMode::Exploring);

  // Cancel (Esc/B) closes it too.
  press(engine, corundum::input::Action::Menu);
  REQUIRE(engine.scene.mode() == GameMode::Menu);
  press(engine, corundum::input::Action::Cancel);
  CHECK(engine.scene.mode() == GameMode::Exploring);

  engine.cleanup();
}

TEST_CASE("pause menu: does not open over another screen") {
  corundum::Engine engine{};
  init_engine(engine);
  engine.scene.ui.push(GameMode::Journal);

  // Journal is an engine screen now, so it owns the step; the menu must still not open over it.
  press(engine, corundum::input::Action::Menu);
  CHECK(engine.scene.mode() == GameMode::Journal);

  engine.cleanup();
}

// doctest's REQUIRE/CHECK macros expand to control flow, so the assertion count — not the test's
// logic — dominates this metric.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("pause menu: Esc opens it from a binding table that predates Action::Menu") {
  corundum::Engine engine{};
  init_engine(engine);

  // An older settings file: every default row except Menu's. Loading such a table must still
  // leave Escape bound to Menu (via the shared-default rule), or Esc cannot open the menu.
  nlohmann::json filtered = nlohmann::json::array();
  for (const nlohmann::json &row : corundum::input::serialize(corundum::input::default_bindings())) {
    if (row.at("action").get<std::string>() != "Menu")
      filtered.push_back(row);
  }
  const auto bindings = corundum::input::parse_bindings(filtered, corundum::input::default_bindings());
  REQUIRE(bindings.has_value());
  REQUIRE(engine.input_mapper.set_bindings(*bindings).has_value());

  // Press the physical Esc key and let the mapper decode it through the binding table.
  corundum::input::clear_pressed(engine.input_state);
  engine.input_mapper.begin_poll();
  engine.input_mapper.key(corundum::input::Key::Escape, true);
  engine.input_mapper.end_poll(engine.input_state);
  REQUIRE(engine.input_state.is_pressed(corundum::input::Action::Menu));

  const auto intent = corundum::input::make_input_intent(engine.input_state, engine.input_mapper.last_device());
  CHECK(engine.update_engine_screens(intent));
  CHECK(engine.scene.mode() == GameMode::Menu);

  engine.cleanup();
}

TEST_CASE("pause menu: Resume pops back to Exploring, Settings stacks on top") {
  corundum::Engine engine{};
  init_engine(engine);

  press(engine, corundum::input::Action::Menu);
  REQUIRE(engine.scene.mode() == GameMode::Menu);

  // Default cursor is Resume: Activate closes the menu.
  press(engine, corundum::input::Action::Select);
  CHECK(engine.scene.mode() == GameMode::Exploring);

  // Down to Settings then Activate pushes the settings screen.
  press(engine, corundum::input::Action::Menu);
  press(engine, corundum::input::Action::MoveDown);
  CHECK(engine.menu.cursor == 1);
  press(engine, corundum::input::Action::Select);
  CHECK(engine.scene.mode() == GameMode::Settings);

  // Cancel peels one layer: Settings → Menu → Exploring.
  press(engine, corundum::input::Action::Cancel);
  CHECK(engine.scene.mode() == GameMode::Menu);
  press(engine, corundum::input::Action::Cancel);
  CHECK(engine.scene.mode() == GameMode::Exploring);

  engine.cleanup();
}

TEST_CASE("settings: TabNext/TabPrev switch pages and reset the cursor") {
  corundum::Engine engine{};
  init_engine(engine);
  open_settings(engine);
  REQUIRE(engine.scene.mode() == GameMode::Settings);
  CHECK(engine.settings_screen.tab == corundum::ui::SettingsTab::General);

  press(engine, corundum::input::Action::MoveDown);
  CHECK(engine.settings_screen.cursor == 1);

  press(engine, corundum::input::Action::TabNext);
  CHECK(engine.settings_screen.tab == corundum::ui::SettingsTab::Controls);
  CHECK(engine.settings_screen.cursor == 0);

  press(engine, corundum::input::Action::TabPrev);
  CHECK(engine.settings_screen.tab == corundum::ui::SettingsTab::General);
  CHECK(engine.settings_screen.cursor == 0);

  engine.cleanup();
}

TEST_CASE("settings: Menu/Start closes the settings screen back to the menu") {
  corundum::Engine engine{};
  init_engine(engine);
  open_settings(engine);
  REQUIRE(engine.scene.mode() == GameMode::Settings);

  press(engine, corundum::input::Action::Menu);
  CHECK(engine.scene.mode() == GameMode::Menu);

  engine.cleanup();
}

TEST_CASE("settings: Left/Right edits master volume, text speed, and UI scale") {
  corundum::Engine engine{};
  init_engine(engine);
  open_settings(engine);

  // Row 0: Master Volume (default 1.0).
  press(engine, corundum::input::Action::MoveLeft);
  CHECK(engine.audio.master_volume() == doctest::Approx(0.9f));
  press(engine, corundum::input::Action::MoveRight);
  CHECK(engine.audio.master_volume() == doctest::Approx(1.0f));

  // Row 1: Text Speed (default Normal = 1.0).
  press(engine, corundum::input::Action::MoveDown);
  press(engine, corundum::input::Action::MoveRight);
  CHECK(engine.render.text_speed == doctest::Approx(2.0f));

  // Row 2: UI Scale (default 1.0) — advances the font sizes and margins in the panel style.
  press(engine, corundum::input::Action::MoveDown);
  press(engine, corundum::input::Action::MoveRight);
  CHECK(engine.render.ui_scale == doctest::Approx(1.25f));
  CHECK(engine.render.panel_skin.style.font_size_body == 28); // lround(22 * 1.25)
  CHECK(engine.render.panel_skin.style.line_spacing == doctest::Approx(40.f));

  // Row 3: Window Mode toggles on Activate.
  press(engine, corundum::input::Action::MoveDown);
  CHECK(engine.window->window_mode() == corundum::core::WindowMode::Windowed);
  press(engine, corundum::input::Action::Select);
  CHECK(engine.window->window_mode() == corundum::core::WindowMode::Fullscreen);
  press(engine, corundum::input::Action::Select);
  CHECK(engine.window->window_mode() == corundum::core::WindowMode::Windowed);

  engine.cleanup();
}

TEST_CASE("settings: Activate on the Controls tab captures the next input as a rebind") {
  corundum::Engine engine{};
  init_engine(engine);
  open_settings(engine);
  press(engine, corundum::input::Action::TabNext);
  REQUIRE(engine.settings_screen.tab == corundum::ui::SettingsTab::Controls);
  REQUIRE(engine.settings_screen.cursor == 0); // first action: MoveUp

  press(engine, corundum::input::Action::Select);
  CHECK(engine.settings_screen.rebinding);
  CHECK(engine.input_mapper.is_capturing());

  engine.input_mapper.key(corundum::input::Key::K, true);
  press(engine, corundum::input::Action::Cancel); // the press that delivers the capture
  CHECK_FALSE(engine.settings_screen.rebinding);

  const std::vector<corundum::input::PhysicalInput> move_up =
      corundum::input::inputs_for(engine.input_mapper.bindings(), corundum::input::Action::MoveUp);
  CHECK(std::ranges::find(move_up, corundum::input::physical(corundum::input::Key::K)) != move_up.end());
  CHECK(std::ranges::find(move_up, corundum::input::physical(corundum::input::Key::W)) == move_up.end());

  // Escape during a capture cancels instead of binding Escape.
  press(engine, corundum::input::Action::Select);
  REQUIRE(engine.settings_screen.rebinding);
  engine.input_mapper.key(corundum::input::Key::Escape, true);
  press(engine, corundum::input::Action::Cancel);
  CHECK_FALSE(engine.settings_screen.rebinding);

  const std::vector<corundum::input::PhysicalInput> still_move_up =
      corundum::input::inputs_for(engine.input_mapper.bindings(), corundum::input::Action::MoveUp);
  CHECK(std::ranges::find(still_move_up, corundum::input::physical(corundum::input::Key::K)) != still_move_up.end());
  CHECK(std::ranges::find(still_move_up, corundum::input::physical(corundum::input::Key::Escape)) ==
        still_move_up.end());

  engine.cleanup();
}
