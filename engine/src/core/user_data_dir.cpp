// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/user_data_dir.hpp>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace corundum::core {

  namespace {

    // Returns the value of an environment variable when it names an absolute
    // path. Unset, empty, and relative values are all treated as absent — the
    // XDG base-directory rule requires ignoring a relative path, and skipping
    // one keeps this function's absolute-path guarantee.
    [[nodiscard]] std::optional<std::filesystem::path> env_dir(const char *name) {
      const char *const value = std::getenv(name);
      if (value == nullptr || value[0] == '\0')
        return std::nullopt;

      std::filesystem::path path(value);
      if (!path.is_absolute())
        return std::nullopt;
      return path;
    }

  } // namespace

  std::expected<std::filesystem::path, std::string> user_data_dir(std::string_view app_name) {
    if (app_name.empty())
      return std::unexpected("app_name must not be empty");

#ifdef _WIN32
    if (const std::optional<std::filesystem::path> roaming = env_dir("APPDATA"))
      return *roaming / app_name;
    if (const std::optional<std::filesystem::path> profile = env_dir("USERPROFILE"))
      return *profile / "AppData" / "Roaming" / app_name;
    return std::unexpected("APPDATA and USERPROFILE are both unset, empty, or not absolute");
#elifdef __APPLE__
    const std::optional<std::filesystem::path> home = env_dir("HOME");
    if (!home)
      return std::unexpected("HOME is unset, empty, or not absolute");
    return *home / "Library" / "Application Support" / app_name;
#else
    if (const std::optional<std::filesystem::path> xdg = env_dir("XDG_DATA_HOME"))
      return *xdg / app_name;
    const std::optional<std::filesystem::path> home = env_dir("HOME");
    if (!home)
      return std::unexpected("XDG_DATA_HOME and HOME are both unset, empty, or not absolute");
    return *home / ".local" / "share" / app_name;
#endif
  }

} // namespace corundum::core
