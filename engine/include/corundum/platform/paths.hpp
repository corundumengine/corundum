#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

namespace corundum::platform {

  /**
   * @brief Per-user writable application-data directory for @p app_name,
   *        following the OS convention (as Unity, Unreal and Godot do).
   *
   * Provided by the linked platform backend. Pure path computation — does not
   * create the directory.
   *   macOS:   $HOME/Library/Application Support/<app_name>
   *   Linux:   $XDG_DATA_HOME/<app_name>, else $HOME/.local/share/<app_name>
   *   Windows: %APPDATA%\<app_name>  (fallback %USERPROFILE%\AppData\Roaming)
   *
   * @param app_name Non-empty application folder name (e.g. GameConfig::game_id).
   * @return Absolute path, or an error if a required env var is unset.
   */
  [[nodiscard]] std::expected<std::filesystem::path, std::string> user_data_dir(std::string_view app_name);

} // namespace corundum::platform
