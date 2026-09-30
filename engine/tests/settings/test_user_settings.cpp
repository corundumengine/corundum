// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include "temp_dir.hpp"

#include <corundum/core/game_config.hpp>
#include <corundum/core/window_mode.hpp>
#include <corundum/engine.hpp>
#include <corundum/input/action_resolver.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/bindings.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/platform/null/null_platform.hpp>
#include <corundum/settings/user_settings.hpp>

// json_fwd.hpp is include-cleaner's provider for nlohmann::json; json.hpp is still
// required to construct json values (the forward header is incomplete).
#include <nlohmann/json.hpp> // NOLINT(misc-include-cleaner): constructors live in json.hpp
#include <nlohmann/json_fwd.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {

  using corundum::core::WindowMode;
  using corundum::input::Action;
  using corundum::input::action_name;
  using corundum::input::InputDevice;
  using corundum::input::k_max_input_sources;
  using corundum::input::Key;
  using corundum::input::name_of;
  using corundum::input::physical;
  using corundum::settings::UserSettings;

  /// Adopt a NullPlatform so capture()/apply() have a window to read and write.
  void adopt_platform(corundum::Engine &engine, unsigned w, unsigned h) {
    corundum::platform::null::NullPlatform platform{corundum::platform::null::make_null_platform(w, h)};
    corundum::platform::null::adopt_null_platform(engine, platform);
  }

  corundum::test::TempDir temp_dir(std::string_view tag) {
    return corundum::test::TempDir{"crpg_test_settings_", tag};
  }

  void write_file(const fs::path &path, std::string_view content) {
    fs::create_directories(path.parent_path());
    std::ofstream f(path);
    f << content;
  }

  /// A non-default settings value: Fullscreen with MoveUp moved off W onto Q.
  [[nodiscard]] UserSettings make_settings() {
    UserSettings settings;
    settings.bindings = corundum::input::default_bindings();
    corundum::input::rebind(settings.bindings, Action::MoveUp, physical(Key::W), physical(Key::Q));
    settings.window_mode = WindowMode::Fullscreen;
    return settings;
  }

  /// A JSON array of k_max_input_sources + 1 distinct, otherwise valid binding rows.
  [[nodiscard]] nlohmann::json oversized_bindings_json() {
    nlohmann::json rows = nlohmann::json::array();
    std::size_t made{};
    for (std::uint16_t code = 0; code <= 400 && made < k_max_input_sources + 1; ++code) {
      const auto input = physical(static_cast<Key>(code));
      const std::string_view name = name_of(input);
      if (name.empty())
        continue;
      const auto action = static_cast<Action>(made % corundum::input::k_action_count);
      rows.push_back({
          {"action", std::string(action_name(action))},
          {"device", std::string(corundum::input::device_name(InputDevice::Keyboard))},
          {"input", std::string(name)},
      });
      ++made;
    }
    return rows;
  }

  /// Restores the working directory on destruction, for the bare-filename save test.
  struct CwdGuard {
    fs::path saved{fs::current_path()};

    explicit CwdGuard(const fs::path &path) {
      fs::current_path(path);
    }

    ~CwdGuard() {
      std::error_code ignored;
      fs::current_path(saved, ignored);
    }

    CwdGuard(const CwdGuard &) = delete;
    CwdGuard &operator=(const CwdGuard &) = delete;
    CwdGuard(CwdGuard &&) = delete;
    CwdGuard &operator=(CwdGuard &&) = delete;
  };

} // namespace

TEST_CASE("settings: serialize/parse round-trips") {
  const UserSettings settings = make_settings();

  const auto parsed = corundum::settings::parse(corundum::settings::serialize(settings), UserSettings{});
  REQUIRE(parsed.has_value());
  CHECK(parsed->bindings == settings.bindings);
  CHECK(parsed->window_mode == settings.window_mode);
}

TEST_CASE("settings: an absent field keeps its default") {
  const UserSettings defaults = make_settings();

  const nlohmann::json root = {{"schema_version", 1}};
  const auto parsed = corundum::settings::parse(root, defaults);
  REQUIRE(parsed.has_value());
  CHECK(parsed->bindings == defaults.bindings);
  CHECK(parsed->window_mode == defaults.window_mode);
}

TEST_CASE("settings: a bad window_mode is rejected") {
  const nlohmann::json root = {{"schema_version", 1}, {"window_mode", "borderless"}};
  const auto parsed = corundum::settings::parse(root, UserSettings{});
  REQUIRE_FALSE(parsed.has_value());
  CHECK(parsed.error().find("window_mode") != std::string::npos);
}

TEST_CASE("settings: a non-object root is rejected") {
  const UserSettings defaults{};
  for (const nlohmann::json &root : {nlohmann::json::array(), nlohmann::json(nullptr), nlohmann::json(42)}) {
    const auto parsed = corundum::settings::parse(root, defaults);
    REQUIRE_FALSE(parsed.has_value());
    CHECK(parsed.error().find("object") != std::string::npos);
  }
}

TEST_CASE("settings: load rejects a non-object file and leaves the engine unchanged") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const auto dir = temp_dir("non_object");
  const auto path = dir / "settings.json";
  write_file(path, "[]");

  const auto loaded = corundum::settings::load(engine, path);
  REQUIRE_FALSE(loaded.has_value());
  CHECK(loaded.error().find("object") != std::string::npos);
  CHECK(engine.input_mapper.bindings() == corundum::input::default_bindings());
  CHECK(engine.window->window_mode() == WindowMode::Windowed);
}

TEST_CASE("settings: loading a missing file is a no-op") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const auto dir = temp_dir("missing");
  const auto loaded = corundum::settings::load(engine, dir / "settings.json");
  CHECK(loaded.has_value());
  CHECK(engine.input_mapper.bindings() == corundum::input::default_bindings());
  CHECK(engine.window->window_mode() == WindowMode::Windowed);
}

TEST_CASE("settings: save/load round-trips bindings and window mode") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const auto dir = temp_dir("round_trip");
  const auto path = dir / "sub" / "settings.json";

  corundum::input::Bindings bindings = engine.input_mapper.bindings();
  corundum::input::bind(bindings, Action::MoveUp, physical(Key::K));
  REQUIRE(engine.input_mapper.set_bindings(bindings).has_value());
  engine.window->set_window_mode(WindowMode::Fullscreen);

  const auto saved = corundum::settings::save(engine, path);
  REQUIRE(saved.has_value());
  REQUIRE(fs::exists(path));

  REQUIRE(engine.input_mapper.set_bindings(corundum::input::default_bindings()).has_value());
  engine.window->set_window_mode(WindowMode::Windowed);

  const auto loaded = corundum::settings::load(engine, path);
  REQUIRE(loaded.has_value());

  const std::vector<corundum::input::PhysicalInput> move_up =
      corundum::input::inputs_for(engine.input_mapper.bindings(), Action::MoveUp);
  CHECK(std::ranges::find(move_up, physical(Key::K)) != move_up.end());
  CHECK(engine.window->window_mode() == WindowMode::Fullscreen);
}

TEST_CASE("settings: a newer schema version is rejected and leaves the engine unchanged") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const auto dir = temp_dir("newer_schema");
  const auto path = dir / "settings.json";
  write_file(path, R"({"schema_version": 2, "window_mode": "fullscreen"})");

  const auto loaded = corundum::settings::load(engine, path);
  REQUIRE_FALSE(loaded.has_value());
  CHECK(engine.input_mapper.bindings() == corundum::input::default_bindings());
  CHECK(engine.window->window_mode() == WindowMode::Windowed);
}

TEST_CASE("settings: an oversized binding table is rejected and leaves the engine unchanged") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const auto dir = temp_dir("oversized");
  const auto path = dir / "settings.json";
  const nlohmann::json root = {
      {"schema_version", 1},
      {"bindings", oversized_bindings_json()},
  };
  write_file(path, root.dump());

  const auto loaded = corundum::settings::load(engine, path);
  REQUIRE_FALSE(loaded.has_value());
  CHECK(engine.input_mapper.bindings() == corundum::input::default_bindings());
}

TEST_CASE("settings: default_path needs a game_id") {
  corundum::core::GameConfig cfg{};
  const auto path = corundum::settings::default_path(cfg);
  CHECK_FALSE(path.has_value());

  cfg.game_id = "keystone";
  const auto resolved = corundum::settings::default_path(cfg);
  REQUIRE(resolved.has_value());
  CHECK(resolved->filename() == "settings.json");
}

TEST_CASE("settings: the settings file overrides game.json's window mode") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  // Stands in for initialize() applying cfg.window_mode.
  engine.window->set_window_mode(WindowMode::Fullscreen);

  const auto dir = temp_dir("override_mode");
  const auto path = dir / "settings.json";
  write_file(path, R"({"schema_version": 1, "window_mode": "windowed"})");

  const auto loaded = corundum::settings::load(engine, path);
  REQUIRE(loaded.has_value());
  CHECK(engine.window->window_mode() == WindowMode::Windowed);
}

TEST_CASE("settings: an absent window_mode keeps game.json's") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  engine.window->set_window_mode(WindowMode::Fullscreen);

  const auto dir = temp_dir("keep_mode");
  const auto path = dir / "settings.json";
  write_file(path, R"({"schema_version": 1})");

  const auto loaded = corundum::settings::load(engine, path);
  REQUIRE(loaded.has_value());
  CHECK(engine.window->window_mode() == WindowMode::Fullscreen);
}

TEST_CASE("settings: save accepts a bare filename in the working directory") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const auto dir = temp_dir("bare_filename");
  const CwdGuard cwd{dir.path()};

  const auto saved = corundum::settings::save(engine, "settings.json");
  REQUIRE(saved.has_value());
  CHECK(fs::exists(dir.path() / "settings.json"));
}
