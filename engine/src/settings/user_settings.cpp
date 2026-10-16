// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/settings/user_settings.hpp>

#include <corundum/audio/audio_backend.hpp>
#include <corundum/core/schema_version.hpp>
#include <corundum/core/user_storage.hpp>
#include <corundum/core/window_mode.hpp>
#include <corundum/engine.hpp>
#include <corundum/input/bindings.hpp>
#include <corundum/platform/window.hpp>
#include <corundum/render/render_system.hpp>

#include <algorithm>
#include <array>
#include <expected>
#include <format>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace corundum::settings {

  namespace {

    /// Read an optional numeric field into @p out, clamped to [@p min, @p max]. Leaves @p out
    /// unchanged when the field is absent; rejects a present-but-non-numeric value.
    std::expected<void, std::string> read_clamped_float(const nlohmann::json &root, std::string_view key, float min,
                                                        float max, float &out) {
      if (!root.contains(key))
        return {};
      const nlohmann::json &value = root.at(key);
      if (!value.is_number())
        return std::unexpected(std::format("settings '{}' must be a number", key));
      out = std::clamp(value.get<float>(), min, max);
      return {};
    }

    /// Read every bus volume present in @p root into @p settings, each clamped to [0, 1].
    std::expected<void, std::string> read_volumes(const nlohmann::json &root, UserSettings &settings) {
      const std::array<std::pair<std::string_view, float *>, 5> volumes{
          {
              {"ambience_volume", &settings.ambience_volume},
              {"master_volume", &settings.master_volume},
              {"music_volume", &settings.music_volume},
              {"sound_volume", &settings.sound_volume},
              {"voice_volume", &settings.voice_volume},
          },
      };
      for (const auto &[key, volume] : volumes) {
        if (std::expected<void, std::string> read = read_clamped_float(root, key, 0.f, 1.f, *volume); !read)
          return read;
      }
      return {};
    }

  } // namespace

  nlohmann::json serialize(const UserSettings &settings) {
    nlohmann::json j = nlohmann::json::object();
    j["schema_version"] = k_user_settings_schema_version;
    j["window_mode"] = core::window_mode_name(settings.window_mode);
    j["bindings"] = input::serialize(settings.bindings);
    j["ambience_volume"] = settings.ambience_volume;
    j["master_volume"] = settings.master_volume;
    j["music_volume"] = settings.music_volume;
    j["sound_volume"] = settings.sound_volume;
    j["voice_volume"] = settings.voice_volume;
    j["text_speed"] = settings.text_speed;
    j["ui_scale"] = settings.ui_scale;
    return j;
  }

  std::expected<UserSettings, std::string> parse(const nlohmann::json &root, const UserSettings &defaults) {
    // A non-object root still passes the schema-version gate (contains() is false on an array or
    // scalar, reading as legacy version 1), so reject it here rather than parse it as "all defaults".
    if (!root.is_object())
      return std::unexpected("settings must be a JSON object");

    UserSettings settings = defaults;

    if (root.contains("window_mode")) {
      const nlohmann::json &value = root.at("window_mode");
      const std::optional<core::WindowMode> mode =
          value.is_string() ? core::parse_window_mode(value.get<std::string>()) : std::nullopt;
      if (!mode)
        return std::unexpected(R"(settings 'window_mode' must be "windowed" or "fullscreen")");
      settings.window_mode = *mode;
    }

    if (root.contains("bindings")) {
      std::expected<input::Bindings, std::string> bindings =
          input::parse_bindings(root.at("bindings"), defaults.bindings);
      if (!bindings)
        return std::unexpected(std::format("settings: {}", bindings.error()));
      settings.bindings = std::move(*bindings);
    }

    if (std::expected<void, std::string> read = read_volumes(root, settings); !read)
      return std::unexpected(read.error());

    if (std::expected<void, std::string> read = read_clamped_float(root, "text_speed", 0.f, 2.f, settings.text_speed);
        !read)
      return std::unexpected(read.error());

    if (std::expected<void, std::string> read = read_clamped_float(root, "ui_scale", 0.75f, 2.f, settings.ui_scale);
        !read)
      return std::unexpected(read.error());

    return settings;
  }

  UserSettings capture(const Engine &engine) {
    return UserSettings{
        .bindings = engine.input_mapper.bindings(),
        .window_mode = engine.window->window_mode(),
        .ambience_volume = engine.audio.bus_volume(audio::AudioBus::Ambience),
        .master_volume = engine.audio.bus_volume(audio::AudioBus::Master),
        .music_volume = engine.audio.bus_volume(audio::AudioBus::Music),
        .sound_volume = engine.audio.bus_volume(audio::AudioBus::Sound),
        .voice_volume = engine.audio.bus_volume(audio::AudioBus::Voice),
        .text_speed = engine.render.text_speed,
        .ui_scale = engine.render.ui_scale,
    };
  }

  std::expected<void, std::string> apply(Engine &engine, const UserSettings &settings) {
    if (std::expected<void, std::string> result = engine.input_mapper.set_bindings(settings.bindings); !result)
      return result;
    engine.window->set_window_mode(settings.window_mode);
    // Keep cfg in sync: reveal_window() re-applies cfg.window_mode once the window is visible, so
    // without this the loaded mode would be clobbered on the first frame.
    engine.cfg.window_mode = settings.window_mode;
    engine.audio.set_bus_volume(audio::AudioBus::Ambience, settings.ambience_volume);
    engine.audio.set_bus_volume(audio::AudioBus::Master, settings.master_volume);
    engine.audio.set_bus_volume(audio::AudioBus::Music, settings.music_volume);
    engine.audio.set_bus_volume(audio::AudioBus::Sound, settings.sound_volume);
    engine.audio.set_bus_volume(audio::AudioBus::Voice, settings.voice_volume);
    engine.render.text_speed = settings.text_speed;
    engine.render.ui_scale = settings.ui_scale;
    render::configure_panel_style(engine.render, engine.cfg);
    return {};
  }

  std::expected<void, std::string> load(Engine &engine, const core::UserStorage &storage) {
    if (!storage.contains(k_settings_name))
      return {};

    std::expected<nlohmann::json, std::string> root = core::read_json(storage, k_settings_name, "settings JSON");
    if (!root)
      return std::unexpected(root.error());

    // Reject a non-object before prepare_schema_version(): it treats an absent schema_version as
    // legacy version 1 and would try to stamp the field, which throws on an array root.
    if (!root->is_object())
      return std::unexpected("settings must be a JSON object");

    if (std::expected<void, std::string> prepared = core::prepare_schema_version(
            *root, k_user_settings_schema_version, "User settings", std::string{k_settings_name}, {});
        !prepared)
      return std::unexpected(prepared.error());

    std::expected<UserSettings, std::string> parsed = parse(*root, capture(engine));
    if (!parsed)
      return std::unexpected(parsed.error());

    return apply(engine, *parsed);
  }

  std::expected<void, std::string> save(const Engine &engine, core::UserStorage &storage) {
    return core::write_json(storage, k_settings_name, serialize(capture(engine)));
  }

} // namespace corundum::settings
