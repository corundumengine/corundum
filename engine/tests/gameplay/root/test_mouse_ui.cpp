// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/core/math/vec.hpp>
#include <corundum/engine.hpp>
#include <corundum/gameplay/gameplay.hpp>
#include <corundum/gameplay/screens/hub_tabs.hpp>
#include <corundum/gameplay/screens/inventory_panel.hpp>
#include <corundum/gameplay/screens/journal.hpp>
#include <corundum/gameplay/screens/modes.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/input_intent.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/ui/menu.hpp>
#include <corundum/world/scene.hpp>
#include <corundum/world/ui_stack.hpp>

#include "world_transition_fixtures.hpp"

#include <cstddef>
#include <filesystem>

namespace {

  using corundum::test::adopt_platform;
  using corundum::test::make_world_config;
  using corundum::world::GameMode;
  namespace screens = corundum::gameplay::screens;

  void init_engine(corundum::Engine &engine) {
    adopt_platform(engine, 640, 480);
    const std::filesystem::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
    REQUIRE(engine.initialize(make_world_config(fixtures)).has_value());
  }

  corundum::core::math::Vec2 viewport(const corundum::Engine &engine) {
    const auto [width, height] = engine.window->size();
    return {.x = static_cast<float>(width), .y = static_cast<float>(height)};
  }

  /// Route one action press through the engine's UI step (see test_pause_menu.cpp).
  bool press(corundum::Engine &engine, corundum::input::Action action) {
    corundum::input::InputState state{};
    state.pressed.set(static_cast<std::size_t>(action));
    engine.input_state = state;
    const auto intent = corundum::input::make_input_intent(engine.input_state, engine.input_mapper.last_device());
    return engine.update_engine_screens(intent);
  }

  /// Drive one fixed UI step with a synthetic cursor. @p prev is the baseline from the previous
  /// poll; a left click also raises Activate/Select the way the binding table would.
  bool step_mouse(corundum::Engine &engine, corundum::core::math::Vec2 cursor, corundum::core::math::Vec2 prev,
                  bool clicked = false, float scroll = 0.f) {
    engine.input_state = {};
    engine.input_state.mouse_x = cursor.x;
    engine.input_state.mouse_y = cursor.y;
    engine.input_state.prev_mouse_x = prev.x;
    engine.input_state.prev_mouse_y = prev.y;
    engine.input_state.mouse_click_pressed = clicked;
    engine.input_state.scroll_delta_y = scroll;
    if (clicked) {
      engine.input_state.pressed.set(static_cast<std::size_t>(corundum::input::Action::Select));
      engine.input_state.pressed.set(static_cast<std::size_t>(corundum::input::Action::Activate));
    }
    const auto intent = corundum::input::make_input_intent(engine.input_state, engine.input_mapper.last_device());
    return engine.update_engine_screens(intent);
  }

  /// Simulate a physical right mouse press and route it through the current UI step.
  bool press_right_mouse(corundum::Engine &engine) {
    corundum::input::clear_pressed(engine.input_state);
    engine.input_mapper.begin_poll();
    engine.input_mapper.mouse_button(corundum::input::MouseButton::Right, true);
    engine.input_mapper.end_poll(engine.input_state);
    REQUIRE(engine.input_state.is_pressed(corundum::input::Action::Cancel));
    const auto intent = corundum::input::make_input_intent(engine.input_state, engine.input_mapper.last_device());
    return engine.update_engine_screens(intent);
  }

} // namespace

TEST_CASE("mouse: hovering a pause-menu row moves focus, a stationary pointer does not") {
  corundum::Engine engine{};
  init_engine(engine);
  REQUIRE(press(engine, corundum::input::Action::Menu));
  REQUIRE(engine.scene.mode() == GameMode::Menu);

  const corundum::ui::MenuLayout layout = corundum::ui::menu_panel_layout(
      *engine.renderer, engine.render.panel_skin.style, viewport(engine), engine.input_mapper.last_device());
  const float row_x = layout.rows.row_pos.x + 5.f;
  const float row2_y = layout.rows.row_pos.y + (2.5f * layout.rows.row_height);

  step_mouse(engine, {.x = row_x, .y = row2_y}, {.x = row_x - 10.f, .y = row2_y});
  CHECK(engine.menu.cursor == 2);

  // Resting does not re-focus; the cursor stays where it was.
  engine.menu.cursor = 0;
  step_mouse(engine, {.x = row_x, .y = row2_y}, {.x = row_x, .y = row2_y});
  CHECK(engine.menu.cursor == 0);

  engine.cleanup();
}

TEST_CASE("mouse: clicking a pause-menu row focuses and activates it the same step") {
  corundum::Engine engine{};
  init_engine(engine);
  REQUIRE(press(engine, corundum::input::Action::Menu));

  const corundum::ui::MenuLayout layout = corundum::ui::menu_panel_layout(
      *engine.renderer, engine.render.panel_skin.style, viewport(engine), engine.input_mapper.last_device());
  const float row_x = layout.rows.row_pos.x + 5.f;
  const float row1_y = layout.rows.row_pos.y + (1.5f * layout.rows.row_height);

  step_mouse(engine, {.x = row_x, .y = row1_y}, {.x = row_x - 10.f, .y = row1_y}, /*clicked=*/true);
  CHECK(engine.scene.mode() == GameMode::Settings);

  engine.cleanup();
}

TEST_CASE("mouse: the wheel moves the pause-menu selection one row per notch") {
  corundum::Engine engine{};
  init_engine(engine);
  REQUIRE(press(engine, corundum::input::Action::Menu));

  step_mouse(engine, {.x = 0.f, .y = 0.f}, {.x = 0.f, .y = 0.f}, /*clicked=*/false, /*scroll=*/1.f);
  CHECK(engine.menu.cursor == 4); // wheel up wraps to the last row (Quit)
  step_mouse(engine, {.x = 0.f, .y = 0.f}, {.x = 0.f, .y = 0.f}, /*clicked=*/false, /*scroll=*/-1.f);
  CHECK(engine.menu.cursor == 0);

  engine.cleanup();
}

TEST_CASE("mouse: right-click backs out of the pause menu") {
  corundum::Engine engine{};
  init_engine(engine);
  REQUIRE(press(engine, corundum::input::Action::Menu));
  REQUIRE(press_right_mouse(engine));
  CHECK(engine.scene.mode() == GameMode::Exploring);
  engine.cleanup();
}

TEST_CASE("mouse: right-click backs out of a hub tab") {
  corundum::Engine engine{};
  init_engine(engine);
  const corundum::gameplay::Gameplay gameplay{engine};
  REQUIRE(press(engine, corundum::input::Action::Inventory));
  REQUIRE(press_right_mouse(engine));
  CHECK(engine.scene.mode() == GameMode::Exploring);
  engine.cleanup();
}

TEST_CASE("mouse: hovering an inventory row moves focus, and the wheel steps the cursor") {
  corundum::Engine engine{};
  init_engine(engine);
  corundum::gameplay::Gameplay gameplay{engine};
  REQUIRE(press(engine, corundum::input::Action::Inventory));

  // The fixture inventory is empty; supply three rows so there is something to hover.
  gameplay.inventory_lines = {
      {.count = 1, .name = "Sword", .id = "sword"},
      {.count = 1, .name = "Shield", .id = "shield"},
      {.count = 1, .name = "Potion", .id = "potion"},
  };
  gameplay.inventory_cursor = 0;

  const screens::InventoryLayout layout =
      screens::inventory_panel_layout(*engine.renderer, engine.render.panel_skin.style, gameplay.inventory_lines,
                                      gameplay.inventory_cursor, viewport(engine));
  REQUIRE(layout.rows.size() == 3);
  const corundum::core::math::Vec2 row2{
      .x = layout.rows[2].pos.x + 5.f,
      .y = layout.rows[2].pos.y + (layout.rows[2].height * 0.5f),
  };

  step_mouse(engine, row2, {.x = row2.x - 10.f, .y = row2.y});
  CHECK(gameplay.inventory_cursor == 2);

  // A stationary pointer does not re-focus; the wheel moves one row up (wrapping to 1).
  step_mouse(engine, row2, row2, /*clicked=*/false, /*scroll=*/1.f);
  CHECK(gameplay.inventory_cursor == 1);

  engine.cleanup();
}

TEST_CASE("mouse: clicking a hub tab switches to it and the wheel cycles tabs") {
  corundum::Engine engine{};
  init_engine(engine);
  const corundum::gameplay::Gameplay gameplay{engine};
  REQUIRE(press(engine, corundum::input::Action::Inventory));

  const screens::HubTabStrip strip =
      screens::hub_tab_strip(*engine.renderer, engine.render.panel_skin.style, viewport(engine));
  const corundum::core::math::Vec2 map_tab{.x = strip.tabs[3].pos.x + (strip.tabs[3].width * 0.5f), .y = strip.y + 1.f};

  step_mouse(engine, map_tab, {.x = map_tab.x - 10.f, .y = map_tab.y}, /*clicked=*/true);
  CHECK(engine.scene.mode() == screens::Map);

  // Wheel up over the strip cycles to the previous tab.
  step_mouse(engine, map_tab, map_tab, /*clicked=*/false, /*scroll=*/1.f);
  CHECK(engine.scene.mode() == screens::Codex);

  engine.cleanup();
}

TEST_CASE("mouse: clicking a journal sub-tab and the wheel over the strip switch sub-tabs") {
  corundum::Engine engine{};
  init_engine(engine);
  const corundum::gameplay::Gameplay gameplay{engine};
  REQUIRE(press(engine, corundum::input::Action::Journal));

  const screens::JournalLayout layout =
      screens::journal_panel_layout(*engine.renderer, engine.render.panel_skin.style, {}, gameplay.journal_screen,
                                    viewport(engine), engine.input_mapper.last_device());
  const auto &completed = layout.sub_tabs[1];
  const corundum::core::math::Vec2 completed_tab{
      .x = completed.pos.x + (completed.width * 0.5f),
      .y = completed.pos.y + (completed.height * 0.5f),
  };

  step_mouse(engine, completed_tab, {.x = completed_tab.x - 10.f, .y = completed_tab.y}, /*clicked=*/true);
  CHECK(gameplay.journal_screen.tab == screens::JournalTab::Completed);

  // Wheel up over the strip cycles to the previous sub-tab.
  step_mouse(engine, completed_tab, completed_tab, /*clicked=*/false, /*scroll=*/1.f);
  CHECK(gameplay.journal_screen.tab == screens::JournalTab::Active);

  engine.cleanup();
}
