// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include "input/bindings_fixtures.hpp"
#include "memory_user_storage.hpp"

#include "null_platform.hpp"
#include <corundum/audio/audio_backend.hpp>
#include <corundum/core/game_config.hpp>
#include <corundum/core/user_storage.hpp>
#include <corundum/core/window_mode.hpp>
#include <corundum/engine.hpp>
#include <corundum/input/action_resolver.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/bindings.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/settings/user_settings.hpp>

// json_fwd.hpp is include-cleaner's provider for nlohmann::json; json.hpp is still
// required to construct json values (the forward header is incomplete).
#include <nlohmann/json.hpp> // NOLINT(misc-include-cleaner): constructors live in json.hpp
#include <nlohmann/json_fwd.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace {

  using corundum::audio::AudioBus;
  using corundum::core::WindowMode;
  using corundum::input::Action;
  using corundum::input::action_name;
  using corundum::input::GamepadControl;
  using corundum::input::InputDevice;
  using corundum::input::k_max_input_sources;
  using corundum::input::Key;
  using corundum::input::name_of;
  using corundum::input::physical;
  using corundum::settings::UserSettings;
  using corundum::test::gamepad_inputs;
  using corundum::test::MemoryUserStorage;

  /// Adopt a NullPlatform so capture()/apply() have a window to read and write.
  void adopt_platform(corundum::Engine &engine, unsigned w, unsigned h) {
    corundum::platform::null::NullPlatform platform{corundum::platform::null::make_null_platform(w, h)};
    corundum::platform::null::adopt_null_platform(engine, platform);
  }

  /// A non-default settings value: Fullscreen with MoveUp moved off W onto Q.
  [[nodiscard]] UserSettings make_settings() {
    UserSettings settings;
    settings.bindings = corundum::input::default_bindings();
    corundum::input::rebind(settings.bindings, Action::MoveUp, physical(Key::W), physical(Key::Q));
    settings.window_mode = WindowMode::Fullscreen;
    settings.master_volume = 0.4f;
    settings.music_volume = 0.6f;
    settings.ambience_volume = 0.2f;
    settings.sound_volume = 0.7f;
    settings.voice_volume = 0.8f;
    settings.text_speed = 2.f;
    settings.ui_scale = 1.25f;
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

} // namespace

TEST_CASE("settings: serialize/parse round-trips") {
  const UserSettings settings = make_settings();

  const auto parsed = corundum::settings::parse(corundum::settings::serialize(settings), UserSettings{});
  REQUIRE(parsed.has_value());
  CHECK(parsed->bindings == settings.bindings);
  CHECK(parsed->window_mode == settings.window_mode);
  CHECK(parsed->master_volume == doctest::Approx(settings.master_volume));
  CHECK(parsed->music_volume == doctest::Approx(settings.music_volume));
  CHECK(parsed->ambience_volume == doctest::Approx(settings.ambience_volume));
  CHECK(parsed->sound_volume == doctest::Approx(settings.sound_volume));
  CHECK(parsed->voice_volume == doctest::Approx(settings.voice_volume));
  CHECK(parsed->text_speed == doctest::Approx(settings.text_speed));
  CHECK(parsed->ui_scale == doctest::Approx(settings.ui_scale));
}

TEST_CASE("settings: out-of-range volume and UI scale are clamped") {
  const nlohmann::json root = {
      {"schema_version", 2}, {"master_volume", 5.f}, {"music_volume", 5.f}, {"ambience_volume", -1.f},
      {"sound_volume", 3.f}, {"voice_volume", -2.f}, {"ui_scale", 0.1f},
  };
  const auto parsed = corundum::settings::parse(root, UserSettings{});
  REQUIRE(parsed.has_value());
  CHECK(parsed->master_volume == doctest::Approx(1.f));
  CHECK(parsed->music_volume == doctest::Approx(1.f));
  CHECK(parsed->ambience_volume == doctest::Approx(0.f));
  CHECK(parsed->sound_volume == doctest::Approx(1.f));
  CHECK(parsed->voice_volume == doctest::Approx(0.f));
  CHECK(parsed->ui_scale == doctest::Approx(0.75f));
}

TEST_CASE("settings: a non-numeric volume is rejected") {
  const nlohmann::json root = {{"schema_version", 2}, {"master_volume", "loud"}};
  const auto parsed = corundum::settings::parse(root, UserSettings{});
  REQUIRE_FALSE(parsed.has_value());
  CHECK(parsed.error().find("master_volume") != std::string::npos);
}

TEST_CASE("settings: a non-numeric music volume is rejected") {
  const nlohmann::json root = {{"schema_version", 2}, {"music_volume", "loud"}};
  const auto parsed = corundum::settings::parse(root, UserSettings{});
  REQUIRE_FALSE(parsed.has_value());
  CHECK(parsed.error().find("music_volume") != std::string::npos);
}

TEST_CASE("settings: a non-numeric sound, ambience or voice volume is rejected, naming its key") {
  for (const std::string_view key : {"sound_volume", "ambience_volume", "voice_volume"}) {
    INFO(key);
    const nlohmann::json root = {{"schema_version", 1}, {std::string{key}, "loud"}};
    const auto parsed = corundum::settings::parse(root, UserSettings{});
    REQUIRE_FALSE(parsed.has_value());
    CHECK(parsed.error().find(key) != std::string::npos);
  }
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

TEST_CASE("settings: load rejects a non-object entry and leaves the engine unchanged") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  MemoryUserStorage storage;
  storage.entries["settings.json"] = "[]";

  const auto loaded = corundum::settings::load(engine, storage);
  REQUIRE_FALSE(loaded.has_value());
  CHECK(loaded.error().find("object") != std::string::npos);
  CHECK(engine.input_mapper.bindings() == corundum::input::default_bindings());
  CHECK(engine.window->window_mode() == WindowMode::Windowed);
}

TEST_CASE("settings: load with no settings entry keeps the engine's settings") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const MemoryUserStorage storage;
  const auto loaded = corundum::settings::load(engine, storage);
  CHECK(loaded.has_value());
  CHECK(engine.input_mapper.bindings() == corundum::input::default_bindings());
  CHECK(engine.window->window_mode() == WindowMode::Windowed);
}

TEST_CASE("settings: save/load round-trips bindings and window mode") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  MemoryUserStorage storage;

  corundum::input::Bindings bindings = engine.input_mapper.bindings();
  corundum::input::bind(bindings, Action::MoveUp, physical(Key::K));
  REQUIRE(engine.input_mapper.set_bindings(bindings).has_value());
  engine.window->set_window_mode(WindowMode::Fullscreen);

  const auto saved = corundum::settings::save(engine, storage);
  REQUIRE(saved.has_value());
  REQUIRE(storage.contains(corundum::settings::k_settings_name));

  REQUIRE(engine.input_mapper.set_bindings(corundum::input::default_bindings()).has_value());
  engine.window->set_window_mode(WindowMode::Windowed);

  const auto loaded = corundum::settings::load(engine, storage);
  REQUIRE(loaded.has_value());

  const std::vector<corundum::input::PhysicalInput> move_up =
      corundum::input::inputs_for(engine.input_mapper.bindings(), Action::MoveUp);
  CHECK(std::ranges::find(move_up, physical(Key::K)) != move_up.end());
  CHECK(engine.window->window_mode() == WindowMode::Fullscreen);
}

TEST_CASE("settings: a Cancel rebind to gamepad A saved before the swap fix reloads without A on Select or Activate") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);
  MemoryUserStorage storage;

  corundum::input::Bindings bindings = corundum::input::default_bindings();
  corundum::input::unbind(bindings, Action::Cancel, physical(GamepadControl::B));
  corundum::input::bind(bindings, Action::Cancel, physical(GamepadControl::A));
  REQUIRE(engine.input_mapper.set_bindings(bindings).has_value());
  REQUIRE(corundum::settings::save(engine, storage).has_value());

  REQUIRE(engine.input_mapper.set_bindings(corundum::input::default_bindings()).has_value());
  REQUIRE(corundum::settings::load(engine, storage).has_value());

  const corundum::input::Bindings &reloaded = engine.input_mapper.bindings();
  CHECK_EQ(gamepad_inputs(reloaded, Action::Cancel),
           std::vector<corundum::input::PhysicalInput>{physical(GamepadControl::A)});
  CHECK(gamepad_inputs(reloaded, Action::Select).empty());
  CHECK(gamepad_inputs(reloaded, Action::Activate).empty());
}

TEST_CASE("settings: a swapped Cancel rebind to gamepad A survives save/load") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);
  MemoryUserStorage storage;

  corundum::input::Bindings bindings = corundum::input::default_bindings();
  corundum::input::bind_swapping(bindings, Action::Cancel, physical(GamepadControl::A));
  REQUIRE(engine.input_mapper.set_bindings(bindings).has_value());
  REQUIRE(corundum::settings::save(engine, storage).has_value());

  REQUIRE(engine.input_mapper.set_bindings(corundum::input::default_bindings()).has_value());
  REQUIRE(corundum::settings::load(engine, storage).has_value());

  const corundum::input::Bindings &reloaded = engine.input_mapper.bindings();
  CHECK_EQ(gamepad_inputs(reloaded, Action::Cancel),
           std::vector<corundum::input::PhysicalInput>{physical(GamepadControl::A)});
  CHECK_EQ(gamepad_inputs(reloaded, Action::Select),
           std::vector<corundum::input::PhysicalInput>{physical(GamepadControl::B)});
  CHECK_EQ(gamepad_inputs(reloaded, Action::Activate),
           std::vector<corundum::input::PhysicalInput>{physical(GamepadControl::B)});
}

TEST_CASE("settings: save/load round-trips the sound, ambience and voice bus volumes") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);
  MemoryUserStorage storage;

  engine.audio.set_bus_volume(AudioBus::Sound, 0.3f);
  engine.audio.set_bus_volume(AudioBus::Ambience, 0.5f);
  engine.audio.set_bus_volume(AudioBus::Voice, 0.7f);
  REQUIRE(corundum::settings::save(engine, storage).has_value());

  for (const AudioBus bus : {AudioBus::Sound, AudioBus::Ambience, AudioBus::Voice})
    engine.audio.set_bus_volume(bus, 1.f);
  REQUIRE(corundum::settings::load(engine, storage).has_value());

  CHECK(engine.audio.bus_volume(AudioBus::Sound) == doctest::Approx(0.3f));
  CHECK(engine.audio.bus_volume(AudioBus::Ambience) == doctest::Approx(0.5f));
  CHECK(engine.audio.bus_volume(AudioBus::Voice) == doctest::Approx(0.7f));
}

TEST_CASE("settings: a newer schema version is rejected and the engine keeps its defaults") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  MemoryUserStorage storage;
  // A v2 file written before the right-mouse Cancel default shipped. There is no migration, so
  // it is refused and the defaults (which include the new binding) stand.
  storage.entries["settings.json"] = R"({"schema_version": 2, "window_mode": "fullscreen"})";

  const auto loaded = corundum::settings::load(engine, storage);
  REQUIRE_FALSE(loaded.has_value());
  CHECK_EQ(loaded.error(),
           std::format("User settings 'settings.json' has schema_version 2, newer than this engine supports (max {}) "
                       "\u2014 update the engine",
                       corundum::settings::k_user_settings_schema_version));
  CHECK(engine.window->window_mode() == WindowMode::Windowed);
  const std::vector<corundum::input::PhysicalInput> cancel =
      corundum::input::inputs_for(engine.input_mapper.bindings(), Action::Cancel);
  CHECK(std::ranges::find(cancel, physical(corundum::input::MouseButton::Right)) != cancel.end());
}

TEST_CASE("settings: an oversized binding table is rejected and leaves the engine unchanged") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  MemoryUserStorage storage;
  const nlohmann::json root = {
      {"schema_version", 1},
      {"bindings", oversized_bindings_json()},
  };
  storage.entries["settings.json"] = root.dump();

  const auto loaded = corundum::settings::load(engine, storage);
  REQUIRE_FALSE(loaded.has_value());
  CHECK_EQ(loaded.error(),
           std::format("settings: binding table has 73 rows; the limit is {}", corundum::input::k_max_input_sources));
  CHECK(engine.input_mapper.bindings() == corundum::input::default_bindings());
}

TEST_CASE("settings: the settings entry overrides game.json's window mode") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  // Stands in for initialize() applying cfg.window_mode.
  engine.window->set_window_mode(WindowMode::Fullscreen);

  MemoryUserStorage storage;
  storage.entries["settings.json"] = R"({"schema_version": 1, "window_mode": "windowed"})";

  const auto loaded = corundum::settings::load(engine, storage);
  REQUIRE(loaded.has_value());
  CHECK(engine.window->window_mode() == WindowMode::Windowed);
}

TEST_CASE("settings: an absent window_mode keeps game.json's") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  engine.window->set_window_mode(WindowMode::Fullscreen);

  MemoryUserStorage storage;
  storage.entries["settings.json"] = R"({"schema_version": 1})";

  const auto loaded = corundum::settings::load(engine, storage);
  REQUIRE(loaded.has_value());
  CHECK(engine.window->window_mode() == WindowMode::Fullscreen);
}

TEST_CASE("settings: apply syncs cfg.window_mode for the deferred reveal") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  corundum::settings::UserSettings settings = corundum::settings::capture(engine);
  settings.window_mode = WindowMode::Fullscreen;
  REQUIRE(corundum::settings::apply(engine, settings).has_value());

  // reveal_window() re-applies cfg.window_mode once the window is visible; apply must update it
  // or the loaded mode would be clobbered on the first frame.
  CHECK(engine.cfg.window_mode == WindowMode::Fullscreen);
}

TEST_CASE("settings: save writes format_json output under settings.json") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);
  MemoryUserStorage storage;
  engine.audio.set_bus_volume(AudioBus::Master, 0.3f);

  REQUIRE(corundum::settings::save(engine, storage).has_value());

  const auto root = corundum::core::read_json(storage, corundum::settings::k_settings_name);
  REQUIRE(root.has_value());
  CHECK(root->at("master_volume").get<float>() == doctest::Approx(0.3f));
}

TEST_CASE("settings: a failed write is reported") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);
  MemoryUserStorage storage;
  storage.injected_write_error = "disk full";

  const auto saved = corundum::settings::save(engine, storage);
  REQUIRE_FALSE(saved.has_value());
  CHECK(saved.error() == "disk full");
}
