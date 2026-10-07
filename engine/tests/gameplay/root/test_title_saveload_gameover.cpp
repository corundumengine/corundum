// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/core/environment.hpp>
#include <corundum/core/game_config.hpp>
#include <corundum/engine.hpp>
#include <corundum/gameplay/gameplay.hpp>
#include <corundum/gameplay/screens/modes.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/input_intent.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/save/save.hpp>
#include <corundum/screen_registry.hpp>
#include <corundum/world/scene.hpp>
#include <corundum/world/ui_stack.hpp>

#include "temp_dir.hpp"
#include "world_transition_fixtures.hpp"

#include "ui/recording_renderer.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace fs = std::filesystem;

namespace {

  using corundum::test::adopt_platform;
  using corundum::test::make_world_config;
  using corundum::test::RecordingRenderer;
  using corundum::world::GameMode;
  namespace screens = corundum::gameplay::screens;

  fs::path fixture_root() {
    return CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
  }

  /// The shared fixture world with a title and a non-empty game id, so saves_directory() works.
  corundum::core::GameConfig make_config(bool show_title) {
    corundum::core::GameConfig cfg = make_world_config(fixture_root());
    cfg.show_title = show_title;
    cfg.game_id = "test_game";
    cfg.title = "Test Quest";
    return cfg;
  }

  void init_engine(corundum::Engine &engine, bool show_title) {
    adopt_platform(engine, 320, 240);
    REQUIRE(engine.initialize(make_config(show_title)).has_value());
  }

  /// Route one physical action press through the engine's UI step, exactly as the fixed-step
  /// loop does.
  bool press(corundum::Engine &engine, corundum::input::Action action) {
    corundum::input::InputState state{};
    state.pressed.set(static_cast<std::size_t>(action));
    engine.input_state = state;
    const auto intent = corundum::input::make_input_intent(engine.input_state, engine.input_mapper.last_device());
    return engine.update_engine_screens(intent);
  }

  /// True when the recorded frame contains a DrawText whose text equals @p text.
  bool rendered_text(const RecordingRenderer &r, std::string_view text) {
    for (const RecordingRenderer::DrawCall &call : r.log) {
      if (const auto *draw = std::get_if<corundum::platform::DrawText>(&call); draw != nullptr && draw->text == text)
        return true;
    }
    return false;
  }

  /// True when any recorded DrawText contains @p needle.
  bool rendered_text_contains(const RecordingRenderer &r, std::string_view needle) {
    for (const RecordingRenderer::DrawCall &call : r.log) {
      if (const auto *draw = std::get_if<corundum::platform::DrawText>(&call);
          draw != nullptr && draw->text.contains(needle))
        return true;
    }
    return false;
  }

  /// Points user_data_dir at a scratch tree for its lifetime, duplicating the guard in
  /// test_save.cpp so these cases never write into the real user data directory.
  struct EnvGuard {
    std::string name;
    std::optional<std::string> saved;

    EnvGuard(std::string_view variable_name, const std::string &value)
        : name(variable_name), saved(corundum::core::read_env(name.c_str())) {
      set_env(name, value);
    }

    ~EnvGuard() {
      if (saved)
        set_env(name, *saved);
      else
        unset_env(name);
    }

    EnvGuard(const EnvGuard &) = delete;
    EnvGuard(EnvGuard &&) = delete;
    EnvGuard &operator=(const EnvGuard &) = delete;
    EnvGuard &operator=(EnvGuard &&) = delete;

  private:
    // setenv/unsetenv are POSIX, declared by the platform's <stdlib.h>.
    // NOLINTBEGIN(misc-include-cleaner)
    static void set_env(const std::string &variable_name, const std::string &value) {
#ifdef _WIN32
      _putenv_s(variable_name.c_str(), value.c_str());
#else
      setenv(variable_name.c_str(), value.c_str(), 1);
#endif
    }

    static void unset_env(const std::string &variable_name) {
#ifdef _WIN32
      _putenv_s(variable_name.c_str(), "");
#else
      unsetenv(variable_name.c_str());
#endif
    }

    // NOLINTEND(misc-include-cleaner)
  };

  struct ScratchUserData {
    corundum::test::TempDir home{"crpg_test_framing_", "user_data"};
    EnvGuard home_guard{"HOME", home.path().string()};
    EnvGuard xdg_guard{"XDG_DATA_HOME", home.path().string()};
#ifdef _WIN32
    EnvGuard appdata_guard{"APPDATA", home.path().string()};
#endif
  };

  /// Overwrite a slot file with deliberately invalid JSON.
  void write_corrupt_slot(const fs::path &directory, std::string_view slot_id) {
    const fs::path path = corundum::save::slot_path(directory, slot_id);
    fs::create_directories(path.parent_path());
    std::ofstream file(path);
    file << "not json";
  }

} // namespace

TEST_CASE("title: opens over the loaded scene and draws its rows") {
  corundum::Engine engine{};
  init_engine(engine, /*show_title=*/true);
  // NOLINTNEXTLINE(misc-const-correctness): constructing Gameplay registers the framework hooks.
  corundum::gameplay::Gameplay gameplay{engine};

  REQUIRE(engine.scene.ui.top() == screens::Title);

  const corundum::ScreenSpec *spec = engine.screens.find(screens::Title);
  REQUIRE(spec != nullptr);
  REQUIRE(spec->render);
  RecordingRenderer r;
  spec->render(engine, r, {.x = 1280.f, .y = 720.f});

  CHECK(rendered_text(r, "Test Quest")); // GameConfig::title, not a framework literal
  CHECK(rendered_text(r, "Continue"));
  CHECK(rendered_text(r, "New Game"));
  CHECK(rendered_text(r, "Load"));
  CHECK(rendered_text(r, "Settings"));
  CHECK(rendered_text(r, "Quit"));

  engine.cleanup();
}

TEST_CASE("title: Continue is disabled with no saves and the cursor starts on New Game") {
  const ScratchUserData user_data;
  corundum::Engine engine{};
  init_engine(engine, /*show_title=*/true);
  // NOLINTNEXTLINE(misc-const-correctness): constructing Gameplay registers the framework hooks.
  corundum::gameplay::Gameplay gameplay{engine};

  CHECK_FALSE(gameplay.title_screen.continue_available);
  CHECK(gameplay.title_screen.cursor == 1); // New Game

  // Down lands on Load, and Up from there skips the disabled Continue back to New Game.
  press(engine, corundum::input::Action::MoveDown);
  CHECK(gameplay.title_screen.cursor == 2);
  press(engine, corundum::input::Action::MoveUp);
  CHECK(gameplay.title_screen.cursor == 1);

  engine.cleanup();
}

TEST_CASE("title: Continue loads the newest valid slot") {
  const ScratchUserData user_data;
  corundum::Engine engine{};
  init_engine(engine, /*show_title=*/true);
  corundum::gameplay::Gameplay gameplay{engine};

  engine.flags["which"] = 2;
  REQUIRE(gameplay.save_to_slot("slot_02").has_value());
  engine.flags["which"] = 5;
  REQUIRE(gameplay.save_to_slot("slot_05").has_value());

  // Forget the last slot and the saved flags, then Continue must restore the newest.
  gameplay.last_slot.clear();
  engine.flags.clear();
  REQUIRE(gameplay.continue_game().has_value());
  CHECK(gameplay.last_slot == "slot_05");
  CHECK(engine.flags["which"] == 5);

  engine.cleanup();
}

TEST_CASE("title: New Game clears the UI stack and resets flags to starting_flags") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);
  corundum::core::GameConfig cfg = make_config(/*show_title=*/true);
  cfg.starting_flags["start.gold"] = 10;
  REQUIRE(engine.initialize(std::move(cfg)).has_value());
  corundum::gameplay::Gameplay gameplay{engine};
  REQUIRE(engine.scene.ui.top() == screens::Title);

  engine.flags["leftover"] = 1;
  engine.flags["start.gold"] = 999;

  REQUIRE(gameplay.new_game().has_value());
  CHECK(engine.scene.ui.empty());
  CHECK_FALSE(engine.flags.contains("leftover"));
  CHECK(engine.flags["start.gold"] == 10);

  engine.cleanup();
}

TEST_CASE("title: New Game starts where a fresh boot starts") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);
  corundum::core::GameConfig cfg = make_config(/*show_title=*/false);
  // A player block that disagrees with the world manifest's default spawn: world mode ignores it
  // (the manifest centre wins), exactly as Engine::initialize() does.
  cfg.player.col = 1.f;
  cfg.player.row = 1.f;
  REQUIRE(engine.initialize(std::move(cfg)).has_value());
  corundum::gameplay::Gameplay gameplay{engine};

  auto &boot = engine.scene.world.transforms;
  const std::uint32_t boot_slot = boot.dense_index(engine.scene.player);
  const float boot_col = boot.col[boot_slot];
  const float boot_row = boot.row[boot_slot];

  // Move the player, then start a new game: it must return to the boot spawn, not cfg.player.
  boot.pos_col(engine.scene.player) = 3.f;
  boot.pos_row(engine.scene.player) = 2.f;
  REQUIRE(gameplay.new_game().has_value());

  const auto &after = engine.scene.world.transforms;
  const std::uint32_t after_slot = after.dense_index(engine.scene.player);
  CHECK(after.col[after_slot] == doctest::Approx(boot_col));
  CHECK(after.row[after_slot] == doctest::Approx(boot_row));

  engine.cleanup();
}

TEST_CASE("title: Load opens SaveLoad and Back returns to Title") {
  const ScratchUserData user_data;
  corundum::Engine engine{};
  init_engine(engine, /*show_title=*/true);
  // NOLINTNEXTLINE(misc-const-correctness): constructing Gameplay registers the framework hooks.
  corundum::gameplay::Gameplay gameplay{engine};

  press(engine, corundum::input::Action::MoveDown); // New Game -> Load
  REQUIRE(gameplay.title_screen.cursor == 2);
  press(engine, corundum::input::Action::Select);
  CHECK(engine.scene.ui.top() == screens::SaveLoad);

  press(engine, corundum::input::Action::Cancel);
  CHECK(engine.scene.ui.top() == screens::Title);

  engine.cleanup();
}

// doctest's REQUIRE/CHECK macros expand to control flow, so the assertion count — not the test's
// logic — dominates this metric.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("save/load: save mode confirms before overwriting and refuses autosave") {
  const ScratchUserData user_data;
  corundum::Engine engine{};
  init_engine(engine, /*show_title=*/false);
  corundum::gameplay::Gameplay gameplay{engine};

  REQUIRE(gameplay.save_to_slot("slot_01").has_value());
  gameplay.open_save_load(/*saving=*/true);
  REQUIRE(engine.scene.ui.top() == screens::SaveLoad);
  gameplay.save_load_screen.cursor = 0; // slot_01, the only slot present

  press(engine, corundum::input::Action::Select);
  CHECK(engine.scene.ui.top() == screens::Confirm);
  press(engine, corundum::input::Action::Select); // Yes
  CHECK(engine.scene.ui.empty());                 // Save/Load popped after the save

  // Autosave exists, so it leads the list; saving it from the UI is refused.
  REQUIRE(gameplay.autosave().has_value());
  gameplay.open_save_load(/*saving=*/true);
  gameplay.save_load_screen.cursor = 0;
  REQUIRE(gameplay.save_load_screen.slots.front().slot_id == "autosave");

  press(engine, corundum::input::Action::Select);
  CHECK(engine.scene.ui.top() == screens::SaveLoad);
  REQUIRE_FALSE(engine.toasts.empty());
  CHECK(engine.toasts.at(engine.toasts.size() - 1).text == "Autosave cannot be overwritten");

  engine.cleanup();
}

// doctest's REQUIRE/CHECK macros expand to control flow, so the assertion count — not the test's
// logic — dominates this metric.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("save/load: a corrupt slot cannot be loaded and shows its error") {
  const ScratchUserData user_data;
  corundum::Engine engine{};
  init_engine(engine, /*show_title=*/false);
  corundum::gameplay::Gameplay gameplay{engine};
  REQUIRE(engine.scene.ui.empty());

  const std::expected<fs::path, std::string> directory = corundum::save::saves_directory(engine.cfg);
  REQUIRE(directory.has_value());
  write_corrupt_slot(*directory, "slot_03");

  gameplay.open_save_load(/*saving=*/false);
  REQUIRE(engine.scene.ui.top() == screens::SaveLoad);

  const int corrupt_index = [&] {
    for (std::size_t i = 0; i < gameplay.save_load_screen.slots.size(); ++i) {
      if (gameplay.save_load_screen.slots[i].slot_id == "slot_03")
        return static_cast<int>(i);
    }
    return -1;
  }();
  REQUIRE(corrupt_index >= 0);
  gameplay.save_load_screen.cursor = corrupt_index;

  const corundum::ScreenSpec *spec = engine.screens.find(screens::SaveLoad);
  REQUIRE(spec != nullptr);
  REQUIRE(spec->render);
  RecordingRenderer r;
  spec->render(engine, r, {.x = 1280.f, .y = 720.f});
  CHECK(rendered_text_contains(r, "Corrupt save"));

  press(engine, corundum::input::Action::Select);
  CHECK(engine.scene.ui.top() == screens::SaveLoad); // still open; nothing loaded

  engine.cleanup();
}

TEST_CASE("game over: Reload is disabled without a last slot and reloads when set") {
  const ScratchUserData user_data;
  corundum::Engine engine{};
  init_engine(engine, /*show_title=*/false);
  corundum::gameplay::Gameplay gameplay{engine};

  gameplay.open_game_over();
  REQUIRE(engine.scene.ui.top() == screens::GameOver);
  CHECK_FALSE(gameplay.game_over_screen.reload_available);
  CHECK(gameplay.game_over_screen.cursor == 1); // Load

  engine.flags["hp"] = 1;
  REQUIRE(gameplay.save_to_slot("slot_01").has_value());
  gameplay.open_game_over();
  REQUIRE(gameplay.game_over_screen.reload_available);
  CHECK(gameplay.game_over_screen.cursor == 0); // Reload

  engine.flags["hp"] = 0;
  press(engine, corundum::input::Action::Select);
  CHECK(engine.flags["hp"] == 1);
  CHECK(engine.scene.ui.empty());

  engine.cleanup();
}

TEST_CASE("game over: Return to Title resets the session") {
  const ScratchUserData user_data;
  corundum::Engine engine{};
  init_engine(engine, /*show_title=*/false);
  corundum::gameplay::Gameplay gameplay{engine};

  engine.scene.ui.push(screens::Inventory);
  gameplay.open_game_over();
  CHECK(gameplay.game_over_screen.reload_available == false);
  gameplay.game_over_screen.cursor = 2; // Return to Title

  press(engine, corundum::input::Action::Select);
  CHECK(engine.scene.ui.top() == screens::Title);

  engine.cleanup();
}

TEST_CASE("game over: a game hook watching a death flag opens it on the next step") {
  corundum::Engine engine{};
  init_engine(engine, /*show_title=*/false);
  corundum::gameplay::Gameplay gameplay{engine};

  // Keystone's pattern: its rules code sets a flag; on_fixed_update clears it and raises GameOver.
  engine.on_fixed_update = [&gameplay](corundum::Engine &e, float) {
    if (!e.flags.contains("player_dead"))
      return;
    e.flags.erase("player_dead");
    gameplay.open_game_over();
  };

  engine.flags["player_dead"] = 1;
  engine.timer.accumulator = engine.timer.target_dt;
  REQUIRE(engine.run_frame());
  CHECK(engine.scene.ui.top() == screens::GameOver);
  CHECK_FALSE(engine.flags.contains("player_dead"));

  engine.cleanup();
}

TEST_CASE("pause menu: does not open over Title, GameOver, Loading or Credits") {
  const ScratchUserData user_data;
  corundum::Engine engine{};
  init_engine(engine, /*show_title=*/false);
  // NOLINTNEXTLINE(misc-const-correctness): constructing Gameplay registers the framework hooks.
  corundum::gameplay::Gameplay gameplay{engine};

  for (const GameMode mode : {screens::Title, screens::GameOver, screens::Loading, screens::Credits}) {
    engine.scene.ui.clear();
    engine.scene.ui.push(mode);
    press(engine, corundum::input::Action::Menu);
    CHECK(engine.scene.ui.top() == mode);
  }

  engine.cleanup();
}
