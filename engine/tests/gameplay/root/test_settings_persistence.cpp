// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include "temp_dir.hpp"
#include "world_transition_fixtures.hpp"

#include <corundum/core/environment.hpp>
#include <corundum/core/json_io.hpp>
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
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

namespace {

  using corundum::input::Action;
  using corundum::test::adopt_platform;
  using corundum::test::make_world_config;
  using corundum::world::GameMode;

  /// Set/unset an environment variable, restoring its prior value on destruction.
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
    // setenv/unsetenv are POSIX, declared by the platform's <stdlib.h>. The only header
    // include-cleaner accepts for them is Darwin's private <_stdlib.h>, which is not portable.
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

  /// Points user_data_dir — and therefore settings::default_path — at a scratch tree for its
  /// lifetime: HOME, XDG_DATA_HOME and (on Windows) APPDATA all resolve into @c home.
  struct ScratchUserData {
    corundum::test::TempDir home{"crpg_test_settings_persist_", "user_data"};
    EnvGuard home_guard{"HOME", home.path().string()};
    EnvGuard xdg_guard{"XDG_DATA_HOME", home.path().string()};
#ifdef _WIN32
    EnvGuard appdata_guard{"APPDATA", home.path().string()};
#endif
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
  init_engine(engine);
  engine.cfg.game_id = "settings_save_game";

  const ScratchUserData scratch;

  // Menu → Settings.
  press(engine, Action::Menu);
  press(engine, Action::MoveDown);
  press(engine, Action::Select);
  REQUIRE(engine.scene.mode() == GameMode::Settings);

  // Master Volume 1.0 → 0.9, then close with Back.
  press(engine, Action::MoveLeft);
  press(engine, Action::Cancel);
  CHECK(engine.scene.mode() == GameMode::Menu); // Settings popped, revealing the pause menu.

  const std::expected<fs::path, std::string> path = corundum::settings::default_path(engine.cfg);
  REQUIRE(path.has_value());
  REQUIRE(fs::exists(*path));

  const std::expected<nlohmann::json, std::string> root = corundum::core::read_json(*path, "settings JSON");
  REQUIRE(root.has_value());
  const std::expected<corundum::settings::UserSettings, std::string> parsed =
      corundum::settings::parse(*root, corundum::settings::UserSettings{});
  REQUIRE(parsed.has_value());
  CHECK(parsed->master_volume == doctest::Approx(0.9f));

  engine.cleanup();
}

TEST_CASE("settings persistence: a project with no game_id saves nothing on close") {
  corundum::Engine engine{};
  init_engine(engine);
  REQUIRE(engine.cfg.game_id.empty());

  press(engine, Action::Menu);
  press(engine, Action::MoveDown);
  press(engine, Action::Select);
  REQUIRE(engine.scene.mode() == GameMode::Settings);
  press(engine, Action::Cancel);

  // default_path() fails on an empty game_id, so no file is written anywhere.
  CHECK_FALSE(corundum::settings::default_path(engine.cfg).has_value());

  engine.cleanup();
}
