// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/settings/user_settings.hpp>

#include <corundum/core/game_config.hpp>
#include <corundum/core/json_io.hpp>
#include <corundum/core/schema_version.hpp>
#include <corundum/core/user_data_dir.hpp>
#include <corundum/core/window_mode.hpp>
#include <corundum/engine.hpp>
#include <corundum/input/bindings.hpp>
#include <corundum/platform/window.hpp>
#include <corundum/render/render_system.hpp>

#include <algorithm>
#include <expected>
#include <filesystem>
#include <format>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace corundum::settings {

  namespace {

    /// v1 → v2 added the optional master_volume / text_speed / ui_scale fields. They are
    /// optional, so a v1 document needs no rewrite; the step only advances the version stamp.
    std::expected<int, std::string> migrate_to_v2(nlohmann::json & /*root*/, int /*from_version*/,
                                                  const std::string & /*path*/) {
      return k_user_settings_schema_version;
    }

    /// Read an optional numeric field, rejecting a present-but-non-numeric value; leaves
    /// @p out unchanged when the field is absent.
    std::expected<void, std::string> read_optional_float(const nlohmann::json &root, std::string_view key, float &out) {
      if (!root.contains(key))
        return {};
      const nlohmann::json &value = root.at(key);
      if (!value.is_number())
        return std::unexpected(std::format("settings '{}' must be a number", key));
      out = value.get<float>();
      return {};
    }

  } // namespace

  nlohmann::json serialize(const UserSettings &settings) {
    nlohmann::json j = nlohmann::json::object();
    j["schema_version"] = k_user_settings_schema_version;
    j["window_mode"] = core::window_mode_name(settings.window_mode);
    j["bindings"] = input::serialize(settings.bindings);
    j["master_volume"] = settings.master_volume;
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

    float master_volume = settings.master_volume;
    if (std::expected<void, std::string> read = read_optional_float(root, "master_volume", master_volume); !read)
      return std::unexpected(read.error());
    settings.master_volume = std::clamp(master_volume, 0.f, 1.f);

    float text_speed = settings.text_speed;
    if (std::expected<void, std::string> read = read_optional_float(root, "text_speed", text_speed); !read)
      return std::unexpected(read.error());
    settings.text_speed = std::clamp(text_speed, 0.f, 2.f);

    float ui_scale = settings.ui_scale;
    if (std::expected<void, std::string> read = read_optional_float(root, "ui_scale", ui_scale); !read)
      return std::unexpected(read.error());
    settings.ui_scale = std::clamp(ui_scale, 0.75f, 2.f);

    return settings;
  }

  UserSettings capture(const Engine &engine) {
    return UserSettings{
        .bindings = engine.input_mapper.bindings(),
        .window_mode = engine.window->window_mode(),
        .master_volume = engine.audio.master_volume(),
        .text_speed = engine.render.text_speed,
        .ui_scale = engine.render.ui_scale,
    };
  }

  std::expected<void, std::string> apply(Engine &engine, const UserSettings &settings) {
    if (std::expected<void, std::string> result = engine.input_mapper.set_bindings(settings.bindings); !result)
      return result;
    engine.window->set_window_mode(settings.window_mode);
    engine.audio.set_master_volume(settings.master_volume);
    engine.render.text_speed = settings.text_speed;
    engine.render.ui_scale = settings.ui_scale;
    render::configure_dialog_style(engine.render, engine.cfg);
    return {};
  }

  std::expected<void, std::string> load(Engine &engine, const std::filesystem::path &path) {
    if (!std::filesystem::exists(path))
      return {};

    std::expected<nlohmann::json, std::string> root = core::read_json(path, "settings JSON");
    if (!root)
      return std::unexpected(root.error());

    // Reject a non-object before prepare_schema_version(): it treats an absent schema_version as
    // legacy version 1 and would try to stamp the field, which throws on an array root.
    if (!root->is_object())
      return std::unexpected("settings must be a JSON object");

    if (std::expected<void, std::string> prepared = core::prepare_schema_version(
            *root, k_user_settings_schema_version, "User settings", path.string(), migrate_to_v2);
        !prepared)
      return std::unexpected(prepared.error());

    std::expected<UserSettings, std::string> parsed = parse(*root, capture(engine));
    if (!parsed)
      return std::unexpected(parsed.error());

    return apply(engine, *parsed);
  }

  std::expected<void, std::string> save(const Engine &engine, const std::filesystem::path &path) {
    // A bare filename has an empty parent path; create_directories("") would fail.
    if (!path.parent_path().empty()) {
      std::error_code directory_error;
      std::filesystem::create_directories(path.parent_path(), directory_error);
      if (directory_error)
        return std::unexpected(
            std::format("cannot create '{}': {}", path.parent_path().string(), directory_error.message()));
    }
    return core::write_json(path, serialize(capture(engine)));
  }

  std::expected<std::filesystem::path, std::string> default_path(const core::GameConfig &cfg) {
    std::expected<std::filesystem::path, std::string> directory = core::user_data_dir(cfg.game_id);
    if (!directory)
      return std::unexpected(directory.error());
    return *directory / "settings.json";
  }

} // namespace corundum::settings
