// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/window_mode.hpp>
#include <corundum/input/bindings.hpp>

#include <nlohmann/json_fwd.hpp>

#include <expected>
#include <string>
#include <string_view>

namespace corundum {
  struct Engine;
} // namespace corundum

namespace corundum::core {
  class UserStorage;
} // namespace corundum::core

namespace corundum::settings {

  /** @brief Current schema version of settings.json. */
  constexpr int k_user_settings_schema_version = 1;

  /** @brief The player's own preferences, persisted per user in settings.json. */
  struct UserSettings {
    input::Bindings bindings;

    core::WindowMode window_mode{core::WindowMode::Windowed};

    /// Ambience-bus volume in [0, 1], applied under master_volume.
    float ambience_volume{1.f};

    /// Master audio volume in [0, 1].
    float master_volume{1.f};

    /// Music volume in [0, 1], applied under master_volume.
    float music_volume{1.f};

    /// Sound-effect bus volume in [0, 1], applied under master_volume.
    float sound_volume{1.f};

    /// Voice (dialogue, narration) bus volume in [0, 1], applied under master_volume.
    float voice_volume{1.f};

    /// Dialogue text-reveal multiplier; 0 reveals instantly (see ui::k_text_speed_presets).
    float text_speed{1.f};

    /// Font and margin scale applied to the in-game screens; 1 is the game.json size.
    float ui_scale{1.f};
  };

  /** @brief settings.json document: schema_version, window_mode, bindings (see input::serialize), the five bus volumes,
   * text_speed and ui_scale. */
  [[nodiscard]] nlohmann::json serialize(const UserSettings &settings);

  /** @brief Parse a settings.json document whose schema_version has already been checked.
   *
   *  An absent field keeps its @p defaults value. Bindings fill gaps from @p defaults.bindings (see
   *  input::parse_bindings).
   *
   *  @return The settings, or an error when @p root is not a JSON object or has a malformed
   *          window_mode or bindings array. A non-object root is rejected rather than read as
   *          "every field absent", so a file corrupted to `[]` or `null` fails to load instead of
   *          loading as defaults and being overwritten on the next save.
   */
  [[nodiscard]] std::expected<UserSettings, std::string> parse(const nlohmann::json &root,
                                                               const UserSettings &defaults);

  /** @brief The engine's live settings: the mapper's bindings and the window's mode.
   *
   *  @pre @p engine.window is non-null (make_engine(), or an adopted platform).
   */
  [[nodiscard]] UserSettings capture(const Engine &engine);

  /** @brief Apply @p settings to the engine's mapper and window.
   *
   *  Also updates `cfg.window_mode` so the deferred first-frame window-mode reveal does not
   *  overwrite the applied mode with the boot config's.
   *
   *  @pre @p engine.window is non-null.
   *  @return An error, leaving the engine unchanged, when the bindings are rejected by InputMapper::set_bindings.
   */
  [[nodiscard]] std::expected<void, std::string> apply(Engine &engine, const UserSettings &settings);

  /** @brief The UserStorage entry the player's settings live in. */
  constexpr std::string_view k_settings_name{"settings.json"};

  /** @brief Load settings from @p storage's k_settings_name entry and apply it, filling gaps from
   *  capture(engine).
   *
   *  A missing entry is not an error: the engine keeps its current settings. Called after
   *  initialize(), a field the entry sets overrides the matching game.json value (window_mode), and a
   *  field it omits keeps it.
   *
   *  @pre @p engine.window is non-null.
   *  @return An error, leaving the engine unchanged, when the entry is unreadable, malformed, from a
   *          newer schema version, or rejected by apply().
   */
  [[nodiscard]] std::expected<void, std::string> load(Engine &engine, const core::UserStorage &storage);

  /** @brief Write capture(engine) to @p storage's k_settings_name entry, replacing it atomically.
   *
   *  @pre @p engine.window is non-null.
   */
  [[nodiscard]] std::expected<void, std::string> save(const Engine &engine, core::UserStorage &storage);

} // namespace corundum::settings
