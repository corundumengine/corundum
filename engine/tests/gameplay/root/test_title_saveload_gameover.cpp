// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include "null_renderer.hpp"
#include <corundum/core/game_config.hpp>
#include <corundum/core/user_storage.hpp>
#include <corundum/engine.hpp>
#include <corundum/gameplay/gameplay.hpp>
#include <corundum/gameplay/screens/modes.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/input_intent.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/render/render_state.hpp>
#include <corundum/save/save.hpp>
#include <corundum/screen_registry.hpp>
#include <corundum/ui/screen_transition.hpp>
#include <corundum/ui/settings.hpp>
#include <corundum/world/scene.hpp>
#include <corundum/world/ui_stack.hpp>

#include "temp_dir.hpp"
#include "world_transition_fixtures.hpp"

#include "ui/recording_renderer.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
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

  /// The shared fixture world with a title and a non-empty game id, so the saves carry a game id.
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

  /// A scratch tree to root the engine's user storage at, standing in for the real user-data dir.
  struct ScratchUserData {
    corundum::test::TempDir home{"crpg_test_framing_", "user_data"};

    /// Give @p engine a user storage rooted at this scratch tree; call before init_engine().
    void attach(corundum::Engine &engine) const {
      engine.user_storage = std::make_unique<corundum::core::DirectoryUserStorage>(home.path());
    }
  };

  /// Overwrite a slot with deliberately invalid JSON.
  void write_corrupt_slot(corundum::core::UserStorage &storage, std::string_view slot_id) {
    REQUIRE(storage.write(corundum::save::slot_name(slot_id), "not json").has_value());
  }

  /// Write @p content to @p path, creating parent directories.
  void write_file(const fs::path &path, std::string_view content) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path);
    file << content;
  }

  /// Render the Loading overlay as run_frame() would; this is what arms Gameplay::advance_loading().
  void render_loading(corundum::Engine &engine) {
    const corundum::ScreenSpec *spec = engine.screens.find(screens::Loading);
    REQUIRE(spec != nullptr);
    REQUIRE(spec->render);
    RecordingRenderer renderer;
    spec->render(engine, renderer, {.x = 320.f, .y = 240.f});
  }

  /// A credits document long enough to overflow the 320x240 test viewport, so the scroll offset
  /// has somewhere to move.
  std::string long_credits_json() {
    std::string lines;
    for (int i = 0; i < 60; ++i) {
      if (i > 0)
        lines += ',';
      lines += (i == 0) ? "\"First line\"" : std::format("\"Line {}\"", i);
    }
    return std::format(R"({{"sections": [{{"heading": "Design", "lines": [{}]}}]}})", lines);
  }

} // namespace

TEST_CASE("title: opens with no world and draws its rows") {
  corundum::Engine engine{};
  init_engine(engine, /*show_title=*/true);
  // NOLINTNEXTLINE(misc-const-correctness): constructing Gameplay registers the framework hooks.
  corundum::gameplay::Gameplay gameplay{engine};

  REQUIRE(engine.scene.ui.top() == screens::Title);
  CHECK(engine.render.mode == corundum::render::RenderMode::None);

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
  user_data.attach(engine);
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
  user_data.attach(engine);
  init_engine(engine, /*show_title=*/true);
  corundum::gameplay::Gameplay gameplay{engine};

  // The title has no world, so create the saves through a real playthrough first.
  REQUIRE(gameplay.new_game().has_value());
  engine.flags["which"] = 2;
  REQUIRE(gameplay.save_to_slot("slot_02").has_value());
  engine.flags["which"] = 5;
  REQUIRE(gameplay.save_to_slot("slot_05").has_value());
  gameplay.return_to_title();

  // Forget the last slot and the saved flags, then Continue must restore the newest.
  gameplay.playthrough.last_slot.clear();
  engine.flags.clear();
  REQUIRE(gameplay.continue_game().has_value());
  CHECK(gameplay.playthrough.last_slot == "slot_05");
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

TEST_CASE("title: New Game starts where a no-title boot starts") {
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
  boot.pos_col_ref(engine.scene.player) = 3.f;
  boot.pos_row_ref(engine.scene.player) = 2.f;
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
  user_data.attach(engine);
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

TEST_CASE("title: Settings opens the framing page and Back returns to Title") {
  const ScratchUserData user_data;
  corundum::Engine engine{};
  user_data.attach(engine);
  init_engine(engine, /*show_title=*/true);
  // NOLINTNEXTLINE(misc-const-correctness): constructing Gameplay registers the framework hooks.
  corundum::gameplay::Gameplay gameplay{engine};

  // No save: cursor starts on New Game (1). Settings is the fourth row, at index 3.
  press(engine, corundum::input::Action::MoveDown); // New Game -> Load
  press(engine, corundum::input::Action::MoveDown); // Load -> Settings
  REQUIRE(gameplay.title_screen.cursor == 3);
  press(engine, corundum::input::Action::Select);

  REQUIRE(engine.scene.ui.top() == GameMode::Settings);
  CHECK(engine.settings_screen.presentation == corundum::ui::SettingsPresentation::Framing);

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
  user_data.attach(engine);
  init_engine(engine, /*show_title=*/false);
  corundum::gameplay::Gameplay gameplay{engine};

  REQUIRE(gameplay.save_to_slot("slot_01").has_value());
  gameplay.open_save_load(corundum::SaveLoadIntent::Save);
  REQUIRE(engine.scene.ui.top() == screens::SaveLoad);
  gameplay.save_load_screen.cursor = 0; // slot_01, the only slot present

  press(engine, corundum::input::Action::Select);
  CHECK(engine.scene.ui.top() == screens::Confirm);
  press(engine, corundum::input::Action::Select); // Yes
  CHECK(engine.scene.ui.empty());                 // Save/Load popped after the save

  // Autosave exists, so it leads the list; saving it from the UI is refused.
  REQUIRE(gameplay.autosave().has_value());
  gameplay.open_save_load(corundum::SaveLoadIntent::Save);
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
  user_data.attach(engine);
  init_engine(engine, /*show_title=*/false);
  corundum::gameplay::Gameplay gameplay{engine};
  REQUIRE(engine.scene.ui.empty());

  REQUIRE(engine.user_storage.has_value());
  write_corrupt_slot(**engine.user_storage, "slot_03");

  gameplay.open_save_load(corundum::SaveLoadIntent::Load);
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
  user_data.attach(engine);
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
  user_data.attach(engine);
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

TEST_CASE("pause menu: does not open over Splash, Title, GameOver, Loading or Credits") {
  const ScratchUserData user_data;
  corundum::Engine engine{};
  user_data.attach(engine);
  init_engine(engine, /*show_title=*/false);
  // NOLINTNEXTLINE(misc-const-correctness): constructing Gameplay registers the framework hooks.
  corundum::gameplay::Gameplay gameplay{engine};

  for (const GameMode mode : {screens::Splash, screens::Title, screens::GameOver, screens::Loading, screens::Credits}) {
    engine.scene.ui.clear();
    engine.scene.ui.push(mode);
    press(engine, corundum::input::Action::Menu);
    CHECK(engine.scene.ui.top() == mode);
  }

  engine.cleanup();
}

TEST_CASE("loading: the queued work runs only after the overlay renders, then pops Loading") {
  const ScratchUserData user_data;
  corundum::Engine engine{};
  user_data.attach(engine);
  init_engine(engine, /*show_title=*/false);
  // NOLINTNEXTLINE(misc-const-correctness): constructing Gameplay registers the framework hooks.
  corundum::gameplay::Gameplay gameplay{engine};

  bool ran = false;
  gameplay.begin_load([&ran](corundum::gameplay::Gameplay &) {
    ran = true;
    return std::expected<void, std::string>{};
  });
  CHECK(engine.scene.ui.top() == screens::Loading);
  CHECK_FALSE(ran);

  // Several steps before the first render must not run the work: the player has seen nothing.
  for (int step = 0; step < 3; ++step)
    press(engine, corundum::input::Action::Select);
  CHECK_FALSE(ran);
  CHECK(engine.scene.ui.top() == screens::Loading);

  render_loading(engine);

  press(engine, corundum::input::Action::Select);
  CHECK(ran);
  CHECK(engine.scene.ui.empty());

  engine.cleanup();
}

TEST_CASE("loading: a failed work raises a toast and still pops the overlay") {
  const ScratchUserData user_data;
  corundum::Engine engine{};
  user_data.attach(engine);
  init_engine(engine, /*show_title=*/false);
  // NOLINTNEXTLINE(misc-const-correctness): constructing Gameplay registers the framework hooks.
  corundum::gameplay::Gameplay gameplay{engine};

  gameplay.begin_load([](corundum::gameplay::Gameplay &) { return std::unexpected(std::string{"disk on fire"}); });
  render_loading(engine);

  press(engine, corundum::input::Action::Select);
  CHECK(engine.scene.ui.empty());
  REQUIRE_FALSE(engine.toasts.empty());
  CHECK(engine.toasts.at(engine.toasts.size() - 1).text.contains("disk on fire"));

  engine.cleanup();
}

TEST_CASE("loading: reset_session_state drops the pending work") {
  const ScratchUserData user_data;
  corundum::Engine engine{};
  user_data.attach(engine);
  init_engine(engine, /*show_title=*/false);
  // NOLINTNEXTLINE(misc-const-correctness): constructing Gameplay registers the framework hooks.
  corundum::gameplay::Gameplay gameplay{engine};

  bool ran = false;
  gameplay.begin_load([&ran](corundum::gameplay::Gameplay &) {
    ran = true;
    return std::expected<void, std::string>{};
  });
  render_loading(engine);

  gameplay.reset_session_state();
  press(engine, corundum::input::Action::Select);
  CHECK_FALSE(ran);

  engine.cleanup();
}

TEST_CASE("credits: a configured file adds the row and the screen scrolls and closes") {
  const ScratchUserData user_data;
  const corundum::test::TempDir credits_dir{"crpg_test_credits_", "screen"};
  const fs::path credits_path = credits_dir / "credits.json";
  write_file(credits_path, long_credits_json());

  corundum::Engine engine{};
  user_data.attach(engine);
  adopt_platform(engine, 320, 240);
  corundum::core::GameConfig cfg = make_config(/*show_title=*/true);
  cfg.paths.credits_file = credits_path.string();
  REQUIRE(engine.initialize(std::move(cfg)).has_value());
  corundum::gameplay::Gameplay gameplay{engine};

  REQUIRE(gameplay.title_screen.credits_available);
  const corundum::ScreenSpec *title_spec = engine.screens.find(screens::Title);
  REQUIRE(title_spec != nullptr);
  REQUIRE(title_spec->render);
  RecordingRenderer title_draw;
  title_spec->render(engine, title_draw, {.x = 1280.f, .y = 720.f});
  CHECK(rendered_text(title_draw, "Credits"));

  // No save exists, so the cursor starts on New Game; Down reaches Credits (Continue is disabled).
  gameplay.title_screen.cursor = 1;
  for (int step = 0; step < 3; ++step)
    press(engine, corundum::input::Action::MoveDown);
  CHECK(gameplay.title_screen.cursor == 4);
  press(engine, corundum::input::Action::Select);
  CHECK(engine.scene.ui.top() == screens::Credits);

  CHECK(gameplay.credits_screen.scroll == doctest::Approx(0.f));
  const float hold_remaining = gameplay.credits_screen.hold;
  REQUIRE(hold_remaining > 0.f);

  // The roll holds still (and the hold drains) before it starts moving.
  press(engine, corundum::input::Action::MoveDown);
  CHECK(gameplay.credits_screen.hold < hold_remaining);
  CHECK(gameplay.credits_screen.scroll == doctest::Approx(0.f));

  // Once the hold elapses the roll advances.
  gameplay.credits_screen.hold = 0.f;
  press(engine, corundum::input::Action::MoveDown);
  CHECK(gameplay.credits_screen.scroll > 0.f);

  press(engine, corundum::input::Action::Cancel);
  CHECK(engine.scene.ui.top() == screens::Title);

  engine.cleanup();
}

TEST_CASE("credits: a missing file leaves the Title without a Credits row") {
  const ScratchUserData user_data;
  corundum::Engine engine{};
  user_data.attach(engine);
  adopt_platform(engine, 320, 240);
  corundum::core::GameConfig cfg = make_config(/*show_title=*/true);
  cfg.paths.credits_file = (fs::path{CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR} / "missing_credits.json").string();
  REQUIRE(engine.initialize(std::move(cfg)).has_value());
  // NOLINTNEXTLINE(misc-const-correctness): constructing Gameplay registers the framework hooks.
  corundum::gameplay::Gameplay gameplay{engine};

  CHECK_FALSE(gameplay.title_screen.credits_available);
  const corundum::ScreenSpec *spec = engine.screens.find(screens::Title);
  REQUIRE(spec != nullptr);
  REQUIRE(spec->render);
  RecordingRenderer r;
  spec->render(engine, r, {.x = 1280.f, .y = 720.f});
  CHECK_FALSE(rendered_text(r, "Credits"));

  engine.cleanup();
}

TEST_CASE("credits: a malformed file leaves the Title without a Credits row") {
  const ScratchUserData user_data;
  const corundum::test::TempDir credits_dir{"crpg_test_credits_", "broken"};
  const fs::path credits_path = credits_dir / "credits.json";
  write_file(credits_path, "not json");

  corundum::Engine engine{};
  user_data.attach(engine);
  adopt_platform(engine, 320, 240);
  corundum::core::GameConfig cfg = make_config(/*show_title=*/true);
  cfg.paths.credits_file = credits_path.string();
  REQUIRE(engine.initialize(std::move(cfg)).has_value());
  // NOLINTNEXTLINE(misc-const-correctness): constructing Gameplay registers the framework hooks.
  corundum::gameplay::Gameplay gameplay{engine};

  CHECK_FALSE(gameplay.title_screen.credits_available);
  const corundum::ScreenSpec *spec = engine.screens.find(screens::Title);
  REQUIRE(spec != nullptr);
  REQUIRE(spec->render);
  RecordingRenderer r;
  spec->render(engine, r, {.x = 1280.f, .y = 720.f});
  CHECK_FALSE(rendered_text(r, "Credits"));

  engine.cleanup();
}

TEST_CASE("credits: a background key is loaded through the renderer") {
  const ScratchUserData user_data;
  const corundum::test::TempDir credits_dir{"crpg_test_credits_", "background"};

  const fs::path with_background = credits_dir / "with_background.json";
  write_file(with_background, R"({"background": "parchment.png", "sections": [{"lines": ["A line"]}]})");

  corundum::Engine engine{};
  user_data.attach(engine);
  adopt_platform(engine, 320, 240);
  corundum::core::GameConfig cfg = make_config(/*show_title=*/true);
  cfg.paths.credits_file = with_background.string();
  REQUIRE(engine.initialize(std::move(cfg)).has_value());
  // NOLINTNEXTLINE(misc-const-correctness): constructing Gameplay registers the framework hooks.
  corundum::gameplay::Gameplay gameplay{engine};
  CHECK(gameplay.credits_screen.background_texture == corundum::platform::null::k_dummy_handle);
  engine.cleanup();
}

TEST_CASE("credits: a file without a background key leaves the texture unset") {
  const ScratchUserData user_data;
  const corundum::test::TempDir credits_dir{"crpg_test_credits_", "background"};

  const fs::path without_background = credits_dir / "without_background.json";
  write_file(without_background, R"({"sections": [{"lines": ["A line"]}]})");

  corundum::Engine engine{};
  user_data.attach(engine);
  adopt_platform(engine, 320, 240);
  corundum::core::GameConfig cfg = make_config(/*show_title=*/true);
  cfg.paths.credits_file = without_background.string();
  REQUIRE(engine.initialize(std::move(cfg)).has_value());
  // NOLINTNEXTLINE(misc-const-correctness): constructing Gameplay registers the framework hooks.
  corundum::gameplay::Gameplay gameplay{engine};
  CHECK(gameplay.credits_screen.background_texture == 0);
  engine.cleanup();
}

TEST_CASE("return to title: unloads the world and clears flags and the playthrough") {
  const ScratchUserData user_data;
  corundum::Engine engine{};
  user_data.attach(engine);
  init_engine(engine, /*show_title=*/false);
  corundum::gameplay::Gameplay gameplay{engine};
  REQUIRE(engine.render.mode != corundum::render::RenderMode::None);

  engine.flags["gold"] = 7;
  gameplay.playthrough.last_slot = "slot_01";
  gameplay.playthrough.playtime_seconds = 42.0;

  gameplay.return_to_title();

  CHECK(engine.render.mode == corundum::render::RenderMode::None);
  CHECK_FALSE(corundum::world::player_present(engine.scene));
  CHECK(engine.flags.empty());
  CHECK(gameplay.playthrough.last_slot.empty());
  CHECK(gameplay.playthrough.playtime_seconds == doctest::Approx(0.0));
  CHECK(engine.scene.mode() == screens::Title);

  engine.cleanup();
}

TEST_CASE("pause menu Quit to Title: confirm Yes returns to the title with no world") {
  const ScratchUserData user_data;
  corundum::Engine engine{};
  user_data.attach(engine);
  init_engine(engine, /*show_title=*/false);
  // NOLINTNEXTLINE(misc-const-correctness): constructing Gameplay registers the framework hooks.
  corundum::gameplay::Gameplay gameplay{engine};
  REQUIRE(engine.render.mode != corundum::render::RenderMode::None);

  engine.flags["gold"] = 7;
  engine.on_menu_quit(engine);
  REQUIRE(engine.scene.mode() == screens::Confirm);
  press(engine, corundum::input::Action::Select); // Yes

  CHECK(engine.render.mode == corundum::render::RenderMode::None);
  CHECK(engine.flags.empty());
  CHECK(engine.scene.mode() == screens::Title);

  engine.cleanup();
}

TEST_CASE("new game: resets playtime, last slot and entered_from_world") {
  const ScratchUserData user_data;
  corundum::Engine engine{};
  user_data.attach(engine);
  init_engine(engine, /*show_title=*/false);
  corundum::gameplay::Gameplay gameplay{engine};

  gameplay.playthrough.playtime_seconds = 50.0;
  gameplay.playthrough.last_slot = "x";
  engine.entered_from_world = true;

  REQUIRE(gameplay.new_game().has_value());

  CHECK(gameplay.playthrough.playtime_seconds == doctest::Approx(0.0));
  CHECK(gameplay.playthrough.last_slot.empty());
  CHECK_FALSE(engine.entered_from_world);

  engine.cleanup();
}

TEST_CASE("load: restores playtime from the save") {
  const ScratchUserData user_data;
  corundum::Engine engine{};
  user_data.attach(engine);
  init_engine(engine, /*show_title=*/false);
  corundum::gameplay::Gameplay gameplay{engine};

  REQUIRE(gameplay.new_game().has_value());
  gameplay.playthrough.playtime_seconds = 123.0;
  REQUIRE(gameplay.save_to_slot("slot_01").has_value());

  gameplay.playthrough.playtime_seconds = 0.0;
  REQUIRE(gameplay.load_from_slot("slot_01").has_value());

  CHECK(gameplay.playthrough.playtime_seconds == doctest::Approx(123.0));
  CHECK(gameplay.playthrough.last_slot == "slot_01");

  engine.cleanup();
}

TEST_CASE("new game: a world that cannot be built falls back to the title") {
  const ScratchUserData user_data;
  corundum::Engine engine{};
  user_data.attach(engine);
  init_engine(engine, /*show_title=*/true);
  corundum::gameplay::Gameplay gameplay{engine};
  REQUIRE(engine.scene.mode() == screens::Title);

  // Break the world source after boot, so the title still opened cleanly.
  engine.cfg.paths.world_manifest_path = (fixture_root() / "worlds/missing/manifest.json").string();

  const std::expected<void, std::string> result = gameplay.new_game();
  CHECK_FALSE(result.has_value());
  CHECK(engine.render.mode == corundum::render::RenderMode::None);
  CHECK(engine.scene.mode() == screens::Title);

  engine.cleanup();
}

TEST_CASE("load: a refusal from the title leaves the title showing") {
  const ScratchUserData user_data;
  corundum::Engine engine{};
  user_data.attach(engine);
  init_engine(engine, /*show_title=*/true);
  corundum::gameplay::Gameplay gameplay{engine};
  REQUIRE(engine.scene.mode() == screens::Title);

  const std::expected<void, std::string> result = gameplay.load_from_slot("slot_09");
  CHECK_FALSE(result.has_value());
  CHECK(engine.scene.mode() == screens::Title);

  engine.cleanup();
}

TEST_CASE("loading: a failed load fades the title back in") {
  const ScratchUserData user_data;
  corundum::Engine engine{};
  user_data.attach(engine);
  init_engine(engine, /*show_title=*/true);
  corundum::gameplay::Gameplay gameplay{engine};

  gameplay.begin_load([](corundum::gameplay::Gameplay &) { return std::unexpected(std::string{"disk on fire"}); });
  render_loading(engine);
  press(engine, corundum::input::Action::Select);

  CHECK(engine.screen_transition.phase() == corundum::ui::TransitionPhase::FadingIn);
  REQUIRE_FALSE(engine.toasts.empty());
  CHECK(engine.toasts.at(engine.toasts.size() - 1).text.contains("Load failed"));

  engine.cleanup();
}

TEST_CASE("no title: the game starts in the world without a fade or toast") {
  const ScratchUserData user_data;
  corundum::Engine engine{};
  user_data.attach(engine);
  init_engine(engine, /*show_title=*/false);
  // NOLINTNEXTLINE(misc-const-correctness): constructing Gameplay registers the framework hooks.
  corundum::gameplay::Gameplay gameplay{engine};

  CHECK(engine.render.mode != corundum::render::RenderMode::None);
  CHECK(engine.toasts.empty());
  CHECK(engine.screen_transition.alpha() == doctest::Approx(0.f));

  engine.cleanup();
}

TEST_CASE("title boot: flags are empty until New Game") {
  const ScratchUserData user_data;
  corundum::Engine engine{};
  user_data.attach(engine);
  adopt_platform(engine, 320, 240);
  corundum::core::GameConfig cfg = make_config(/*show_title=*/true);
  cfg.starting_flags = {{"gold", 5}};
  REQUIRE(engine.initialize(std::move(cfg)).has_value());
  corundum::gameplay::Gameplay gameplay{engine};

  CHECK(engine.flags.empty());

  REQUIRE(gameplay.new_game().has_value());
  CHECK(engine.flags["gold"] == 5);

  engine.cleanup();
}

TEST_CASE("title boot: no world is loaded under the title") {
  const ScratchUserData user_data;
  corundum::Engine engine{};
  user_data.attach(engine);
  init_engine(engine, /*show_title=*/true);
  // NOLINTNEXTLINE(misc-const-correctness): constructing Gameplay registers the framework hooks.
  corundum::gameplay::Gameplay gameplay{engine};

  CHECK(engine.render.mode == corundum::render::RenderMode::None);
  CHECK_FALSE(corundum::world::player_present(engine.scene));
  CHECK(engine.render.area_texture_ids.empty());

  engine.cleanup();
}
