// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

namespace corundum::core {

  /**
   * @brief Per-user writable application-data directory for @p app_name,
   *        following the OS convention (as Unity, Unreal and Godot do).
   *
   * Pure path computation — does not create the directory.
   *   macOS:   $HOME/Library/Application Support/<app_name>
   *   Linux:   $XDG_DATA_HOME/<app_name>, else $HOME/.local/share/<app_name>
   *   Windows: %APPDATA%\<app_name>  (fallback %USERPROFILE%\AppData\Roaming)
   *
   * A candidate variable that is unset, empty, or not an absolute path is
   * skipped in favour of the next one.
   *
   * @param app_name Application folder name (e.g. GameConfig::game_id).
   * @pre app_name is non-empty.
   * @return An absolute path, or an error when @p app_name is empty or no
   *         candidate variable names a usable directory.
   */
  [[nodiscard]] std::expected<std::filesystem::path, std::string> user_data_dir(std::string_view app_name);

} // namespace corundum::core
