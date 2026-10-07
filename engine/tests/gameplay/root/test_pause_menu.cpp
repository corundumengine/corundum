// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/core/window_mode.hpp>
#include <corundum/engine.hpp>
#include <corundum/gameplay/dialogue/conversation.hpp>
#include <corundum/gameplay/dialogue/dialogue.hpp>
#include <corundum/gameplay/gameplay.hpp>
#include <corundum/gameplay/screens/modes.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/bindings.hpp>
#include <corundum/input/input_intent.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/render/render_state.hpp>
#include <corundum/ui/menu.hpp>
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
#include <utility>
#include <vector>

namespace {

  using corundum::test::adopt_platform;
  using corundum::test::make_world_config;
  using corundum::world::GameMode;
  namespace screens = corundum::gameplay::screens;

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

  /// Route Escape — which the default bindings raise as both Cancel and Menu — through the UI
  /// step, so an open screen's back handling wins over opening the pause menu on the same press.
  bool press_escape(corundum::Engine &engine) {
    corundum::input::InputState state{};
    state.pressed.set(static_cast<std::size_t>(corundum::input::Action::Cancel));
    state.pressed.set(static_cast<std::size_t>(corundum::input::Action::Menu));
    engine.input_state = state;
    const auto intent = corundum::input::make_input_intent(engine.input_state, engine.input_mapper.last_device());
    return engine.update_engine_screens(intent);
  }

  corundum::gameplay::dialogue::Graph make_talk_graph() {
    using namespace corundum::gameplay::dialogue;
    Graph graph;
    graph.graph_id = "pause_menu_over_screens";
    graph.speaker = "NPC";

    Node node;
    node.id = "n0";
    node.type = NodeType::Talk;
    node.text = "Hello";
    node.next_id = "end";
    graph.id_to_index[node.id] = graph.nodes.size();
    graph.nodes.push_back(std::move(node));
    return graph;
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

TEST_CASE("pause menu: Start opens it over an open hub tab, Esc closes the tab instead") {
  corundum::Engine engine{};
  init_engine(engine);
  // NOLINTNEXTLINE(misc-const-correctness): constructing Gameplay registers the hub input hook.
  corundum::gameplay::Gameplay gameplay{engine};

  press(engine, corundum::input::Action::Journal);
  REQUIRE(engine.scene.mode() == screens::Journal);

  // Start (Menu without Cancel) stacks the pause menu over the tab; Resume reveals the same tab
  // rather than resetting to Exploring, because the tab layer was only covered, never popped.
  CHECK(press(engine, corundum::input::Action::Menu));
  CHECK(engine.scene.mode() == GameMode::Menu);
  press(engine, corundum::input::Action::Select);
  CHECK(engine.scene.mode() == screens::Journal);

  // Esc raises Cancel and Menu together: the tab's own back handling wins and no menu opens.
  CHECK(press_escape(engine));
  CHECK(engine.scene.mode() == GameMode::Exploring);

  engine.cleanup();
}

TEST_CASE("pause menu: Start opens over dialogue and Resume returns to it") {
  corundum::Engine engine{};
  init_engine(engine);
  // The conversation holds a non-owning pointer to the graph, so it must outlive the test.
  const corundum::gameplay::dialogue::Graph graph = make_talk_graph();
  corundum::gameplay::Gameplay gameplay{engine};
  gameplay.dialogue.emplace(graph, engine.flags);
  engine.scene.ui.push(screens::Dialogue);
  REQUIRE(engine.scene.mode() == screens::Dialogue);

  CHECK(press(engine, corundum::input::Action::Menu));
  CHECK(engine.scene.mode() == GameMode::Menu);

  press(engine, corundum::input::Action::Select);
  CHECK(engine.scene.mode() == screens::Dialogue);
  CHECK(gameplay.dialogue->is_active());

  engine.cleanup();
}

TEST_CASE("pause menu: Esc over dialogue closes the dialogue without opening the menu") {
  corundum::Engine engine{};
  init_engine(engine);
  // The conversation holds a non-owning pointer to the graph, so it must outlive the test.
  const corundum::gameplay::dialogue::Graph graph = make_talk_graph();
  corundum::gameplay::Gameplay gameplay{engine};
  gameplay.dialogue.emplace(graph, engine.flags);
  engine.scene.ui.push(screens::Dialogue);
  REQUIRE(engine.scene.mode() == screens::Dialogue);

  // The engine step must not open a menu; the dialogue's own back handling runs in the fixed
  // step afterward and closes the conversation.
  CHECK_FALSE(press_escape(engine));
  CHECK(engine.scene.mode() == screens::Dialogue);
  gameplay.fixed_step(1.f / 60.f);
  CHECK(engine.scene.mode() == GameMode::Exploring);
  CHECK_FALSE(gameplay.dialogue.has_value());

  engine.cleanup();
}

TEST_CASE("pause menu: Menu with it already open closes it without growing the stack") {
  corundum::Engine engine{};
  init_engine(engine);
  press(engine, corundum::input::Action::Menu);
  REQUIRE(engine.scene.mode() == GameMode::Menu);
  REQUIRE(engine.scene.ui.size() == 1);

  press(engine, corundum::input::Action::Menu);
  CHECK(engine.scene.mode() == GameMode::Exploring);
  CHECK(engine.scene.ui.empty());

  engine.cleanup();
}

TEST_CASE("pause menu: Menu over the settings screen closes settings") {
  corundum::Engine engine{};
  init_engine(engine);
  open_settings(engine);
  REQUIRE(engine.scene.mode() == GameMode::Settings);

  press(engine, corundum::input::Action::Menu);
  CHECK(engine.scene.mode() == GameMode::Menu);

  engine.cleanup();
}

TEST_CASE("pause menu: a rebind-capture press never opens the menu") {
  corundum::Engine engine{};
  init_engine(engine);
  open_settings(engine);
  press(engine, corundum::input::Action::TabNext);
  REQUIRE(engine.settings_screen.tab == corundum::ui::SettingsTab::Controls);
  press(engine, corundum::input::Action::Select);
  REQUIRE(engine.settings_screen.rebinding);

  // The press the settings screen is capturing must not also open the pause menu over it.
  press(engine, corundum::input::Action::Menu);
  CHECK(engine.scene.mode() == GameMode::Settings);
  CHECK(engine.scene.ui.size() == 2);

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

TEST_CASE("pause menu: rows are Resume / Settings / Save / Load / Quit") {
  using corundum::ui::MenuCommand;
  CHECK(corundum::ui::k_menu_command_count == 5);
  CHECK(corundum::ui::menu_command_at(0) == MenuCommand::Resume);
  CHECK(corundum::ui::menu_command_at(1) == MenuCommand::Settings);
  CHECK(corundum::ui::menu_command_at(2) == MenuCommand::Save);
  CHECK(corundum::ui::menu_command_at(3) == MenuCommand::Load);
  CHECK(corundum::ui::menu_command_at(4) == MenuCommand::Quit);
  CHECK(corundum::ui::menu_command_label(MenuCommand::Save) == "Save");
  CHECK(corundum::ui::menu_command_label(MenuCommand::Load) == "Load");
}

// The Save/Load entries do not save or load themselves; they raise QuickSave/QuickLoad one fixed
// step later so the menu's own Activate press cannot reach the simulation. The game observes the
// raised action in on_fixed_update.
// doctest's REQUIRE/CHECK macros expand to control flow, so the assertion count — not the test's
// logic — dominates this metric.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("pause menu: Save closes the menu and raises QuickSave exactly once") {
  corundum::Engine engine{};
  init_engine(engine);

  int quick_saves = 0;
  int quick_loads = 0;
  engine.on_fixed_update = [&](corundum::Engine &e, float) {
    if (e.input_state.is_pressed(corundum::input::Action::QuickSave))
      ++quick_saves;
    if (e.input_state.is_pressed(corundum::input::Action::QuickLoad))
      ++quick_loads;
  };

  press(engine, corundum::input::Action::Menu);
  press(engine, corundum::input::Action::MoveDown); // Resume → Settings
  press(engine, corundum::input::Action::MoveDown); // Settings → Save
  REQUIRE(engine.menu.cursor == 2);
  press(engine, corundum::input::Action::Select);
  CHECK(engine.scene.mode() == GameMode::Exploring);
  CHECK(quick_saves == 0); // not promoted until a screen-free fixed step runs

  engine.timer.accumulator = engine.timer.target_dt;
  REQUIRE(engine.run_frame());
  CHECK(quick_saves == 1);
  CHECK(quick_loads == 0);

  engine.timer.accumulator = engine.timer.target_dt;
  REQUIRE(engine.run_frame());
  CHECK(quick_saves == 1); // observed exactly once, then cleared

  engine.cleanup();
}

// doctest's REQUIRE/CHECK macros expand to control flow, so the assertion count — not the test's
// logic — dominates this metric.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("pause menu: Load closes the menu and raises QuickLoad exactly once") {
  corundum::Engine engine{};
  init_engine(engine);

  int quick_loads = 0;
  engine.on_fixed_update = [&](corundum::Engine &e, float) {
    if (e.input_state.is_pressed(corundum::input::Action::QuickLoad))
      ++quick_loads;
  };

  press(engine, corundum::input::Action::Menu);
  press(engine, corundum::input::Action::MoveDown);
  press(engine, corundum::input::Action::MoveDown);
  press(engine, corundum::input::Action::MoveDown); // Save → Load
  REQUIRE(engine.menu.cursor == 3);
  press(engine, corundum::input::Action::Select);
  CHECK(engine.scene.mode() == GameMode::Exploring);

  engine.timer.accumulator = engine.timer.target_dt;
  REQUIRE(engine.run_frame());
  CHECK(quick_loads == 1);

  engine.timer.accumulator = engine.timer.target_dt;
  REQUIRE(engine.run_frame());
  CHECK(quick_loads == 1);

  engine.cleanup();
}

TEST_CASE("pause menu: Save with no on_fixed_update hook does not crash") {
  corundum::Engine engine{};
  init_engine(engine);

  press(engine, corundum::input::Action::Menu);
  press(engine, corundum::input::Action::MoveDown);
  press(engine, corundum::input::Action::MoveDown);
  press(engine, corundum::input::Action::Select);
  CHECK(engine.scene.mode() == GameMode::Exploring);

  engine.timer.accumulator = engine.timer.target_dt;
  CHECK(engine.run_frame());

  engine.cleanup();
}

namespace {

  /// Move the pause-menu cursor onto the Quit row and Activate it.
  void activate_quit(corundum::Engine &engine) {
    press(engine, corundum::input::Action::Menu);
    for (int row = 0; row < corundum::ui::k_menu_command_count - 1; ++row)
      press(engine, corundum::input::Action::MoveDown);
    REQUIRE(engine.menu.cursor == corundum::ui::k_menu_command_count - 1);
    press(engine, corundum::input::Action::Select);
  }

} // namespace

TEST_CASE("pause menu: Quit calls on_menu_quit instead of requesting quit") {
  corundum::Engine engine{};
  init_engine(engine);

  int quits = 0;
  engine.on_menu_quit = [&quits](corundum::Engine &) { ++quits; };

  activate_quit(engine);
  CHECK(quits == 1);
  CHECK_FALSE(engine.quit_requested());

  engine.cleanup();
}

TEST_CASE("pause menu: Quit requests quit when no on_menu_quit hook is set") {
  corundum::Engine engine{};
  init_engine(engine);
  REQUIRE_FALSE(engine.quit_requested());

  activate_quit(engine);
  CHECK(engine.quit_requested());

  engine.cleanup();
}

TEST_CASE("pause menu: blocks_pause_menu suppresses opening the menu") {
  corundum::Engine engine{};
  init_engine(engine);
  // NOLINTNEXTLINE(misc-const-correctness): constructing Gameplay registers the hub input hook.
  corundum::gameplay::Gameplay gameplay{engine};
  engine.blocks_pause_menu = [](corundum::world::GameMode mode) { return mode == screens::Journal; };

  press(engine, corundum::input::Action::Journal);
  REQUIRE(engine.scene.mode() == screens::Journal);

  press(engine, corundum::input::Action::Menu);
  CHECK(engine.scene.mode() == screens::Journal);

  engine.cleanup();
}

// doctest's REQUIRE/CHECK macros expand to control flow, so the assertion count — not the test's
// logic — dominates this metric.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("confirm: Yes runs on_yes and pops, No and Back do not") {
  corundum::Engine engine{};
  init_engine(engine);
  corundum::gameplay::Gameplay gameplay{engine};

  int yes_calls = 0;
  gameplay.open_confirm("Quit to Title?", [&yes_calls](corundum::gameplay::Gameplay &) { ++yes_calls; });
  REQUIRE(engine.scene.mode() == screens::Confirm);
  REQUIRE(engine.scene.ui.size() == 1);
  CHECK(gameplay.confirm.question == "Quit to Title?");

  // Yes is the default highlight.
  press(engine, corundum::input::Action::Select);
  CHECK(yes_calls == 1);
  CHECK(engine.scene.mode() == GameMode::Exploring);
  CHECK(gameplay.confirm.question.empty());
  CHECK_FALSE(gameplay.confirm.on_yes);

  // No: toggle to the second option, then Activate.
  yes_calls = 0;
  gameplay.open_confirm("Quit to Title?", [&yes_calls](corundum::gameplay::Gameplay &) { ++yes_calls; });
  press(engine, corundum::input::Action::MoveRight);
  REQUIRE_FALSE(gameplay.confirm.yes_selected);
  press(engine, corundum::input::Action::Select);
  CHECK(yes_calls == 0);
  CHECK(engine.scene.mode() == GameMode::Exploring);

  // Back closes without running on_yes.
  yes_calls = 0;
  gameplay.open_confirm("Quit to Title?", [&yes_calls](corundum::gameplay::Gameplay &) { ++yes_calls; });
  press(engine, corundum::input::Action::Cancel);
  CHECK(yes_calls == 0);
  CHECK(engine.scene.mode() == GameMode::Exploring);

  engine.cleanup();
}
