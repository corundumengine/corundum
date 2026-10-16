// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include "temp_dir.hpp"
#include "world_transition_fixtures.hpp"

#include <corundum/core/user_storage.hpp>
#include <corundum/engine.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/input_intent.hpp>
#include <corundum/settings/user_settings.hpp>
#include <corundum/world/scene.hpp>
#include <corundum/world/ui_stack.hpp>

// json_fwd.hpp is include-cleaner's provider for nlohmann::json; json.hpp is still
// required to construct json values (the forward header is incomplete).
#include <nlohmann/json.hpp> // NOLINT(misc-include-cleaner): constructors live in json.hpp
#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>

namespace fs = std::filesystem;

namespace {

  using corundum::input::Action;
  using corundum::test::adopt_platform;
  using corundum::test::make_world_config;
  using corundum::world::GameMode;

  /// A scratch tree to root the engine's user storage at, standing in for the real user-data dir.
  struct ScratchUserData {
    corundum::test::TempDir home{"crpg_test_settings_persist_", "user_data"};

    /// Give @p engine a user storage rooted at this scratch tree; call before init_engine().
    void attach(corundum::Engine &engine) const {
      engine.user_storage = std::make_unique<corundum::core::DirectoryUserStorage>(home.path());
    }
  };

  void init_engine(corundum::Engine &engine) {
    adopt_platform(engine, 320, 240);
    const fs::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
    REQUIRE(engine.initialize(make_world_config(fixtures)).has_value());
  }

  /// Route one action press through the engine's UI step, exactly as the fixed-step loop does.
  void press(corundum::Engine &engine, Action action) {
    corundum::input::InputState state{};
    state.pressed.set(static_cast<std::size_t>(action));
    engine.input_state = state;
    const auto intent = corundum::input::make_input_intent(engine.input_state, engine.input_mapper.last_device());
    static_cast<void>(engine.update_engine_screens(intent));
  }

} // namespace

TEST_CASE("settings persistence: closing the settings screen writes settings.json") {
  corundum::Engine engine{};
  const ScratchUserData user_data;
  user_data.attach(engine);
  init_engine(engine);
  engine.cfg.game_id = "settings_save_game";

  // Menu → Settings.
  press(engine, Action::Menu);
  press(engine, Action::MoveDown);
  press(engine, Action::Select);
  REQUIRE(engine.scene.mode() == GameMode::Settings);

  // Master Volume 1.0 → 0.9, then close with Back.
  press(engine, Action::MoveLeft);
  press(engine, Action::Cancel);
  CHECK(engine.scene.mode() == GameMode::Menu); // Settings popped, revealing the pause menu.

  REQUIRE(engine.user_storage.has_value());
  REQUIRE((*engine.user_storage)->contains(corundum::settings::k_settings_name));

  const std::expected<nlohmann::json, std::string> root =
      corundum::core::read_json(**engine.user_storage, corundum::settings::k_settings_name, "settings JSON");
  REQUIRE(root.has_value());
  const std::expected<corundum::settings::UserSettings, std::string> parsed =
      corundum::settings::parse(*root, corundum::settings::UserSettings{});
  REQUIRE(parsed.has_value());
  CHECK(parsed->master_volume == doctest::Approx(0.9f));

  engine.cleanup();
}

TEST_CASE("settings persistence: a project with no user storage saves nothing on close") {
  corundum::Engine engine{};
  init_engine(engine);
  REQUIRE(engine.cfg.game_id.empty());
  REQUIRE_FALSE(engine.user_storage.has_value()); // No game_id, so no user storage.

  press(engine, Action::Menu);
  press(engine, Action::MoveDown);
  press(engine, Action::Select);
  REQUIRE(engine.scene.mode() == GameMode::Settings);
  press(engine, Action::Cancel);

  // user_storage holds an error, so closing Settings writes nothing and logs no warning.
  CHECK_FALSE(engine.user_storage.has_value());

  engine.cleanup();
}
