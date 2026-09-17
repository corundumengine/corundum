// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/core/user_data_dir.hpp>

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace {

  // Saves the current value of an environment variable, installs the requested
  // state for the guard's lifetime, and restores the original on destruction, so
  // tests can mutate process env vars without leaking state into later cases.
  struct EnvGuard {
    std::string name;
    std::optional<std::string> saved;

    /// Removes the variable for the guard's lifetime.
    explicit EnvGuard(std::string_view variable_name) : name(variable_name), saved(read_env(name)) {
      unset_env(name);
    }

    /// Sets the variable to @p value for the guard's lifetime.
    EnvGuard(std::string_view variable_name, const std::string &value) : name(variable_name), saved(read_env(name)) {
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
    [[nodiscard]] static std::optional<std::string> read_env(const std::string &variable_name) {
      const char *const value = std::getenv(variable_name.c_str());
      if (value == nullptr)
        return std::nullopt;
      return std::string(value);
    }

    // setenv/unsetenv are POSIX, declared by the platform's <stdlib.h>.
    // include-cleaner's only accepted header for them is Darwin's private
    // <_stdlib.h>, which is not portable.
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

  [[nodiscard]] std::string path_str(const std::filesystem::path &p) {
    return p.string();
  }

} // namespace

TEST_CASE("user_data_dir — empty app_name is rejected") {
  const auto result = corundum::core::user_data_dir("");
  CHECK_FALSE(result.has_value());
}

#ifdef __APPLE__
TEST_CASE("user_data_dir — macOS resolves $HOME/Library/Application Support/<app>") {
  const EnvGuard home("HOME", "/tmp/testhome");

  const auto result = corundum::core::user_data_dir("keystone");
  REQUIRE(result.has_value());
  CHECK(path_str(*result) ==
        path_str(std::filesystem::path("/tmp/testhome") / "Library" / "Application Support" / "keystone"));
}

TEST_CASE("user_data_dir — macOS errors when HOME is unset") {
  const EnvGuard home("HOME");

  const auto result = corundum::core::user_data_dir("keystone");
  CHECK_FALSE(result.has_value());
}

TEST_CASE("user_data_dir — macOS errors when HOME is relative") {
  const EnvGuard home("HOME", "relative/home");

  const auto result = corundum::core::user_data_dir("keystone");
  CHECK_FALSE(result.has_value());
}
#endif

#ifdef __linux__
TEST_CASE("user_data_dir — Linux prefers $XDG_DATA_HOME/<app>") {
  const EnvGuard xdg("XDG_DATA_HOME", "/tmp/testxdg");
  const EnvGuard home("HOME", "/tmp/testhome");

  const auto result = corundum::core::user_data_dir("keystone");
  REQUIRE(result.has_value());
  CHECK(path_str(*result) == path_str(std::filesystem::path("/tmp/testxdg") / "keystone"));
}

TEST_CASE("user_data_dir — Linux falls back to $HOME/.local/share/<app>") {
  const EnvGuard xdg("XDG_DATA_HOME");
  const EnvGuard home("HOME", "/tmp/testhome");

  const auto result = corundum::core::user_data_dir("keystone");
  REQUIRE(result.has_value());
  CHECK(path_str(*result) == path_str(std::filesystem::path("/tmp/testhome") / ".local" / "share" / "keystone"));
}

TEST_CASE("user_data_dir — Linux errors when both XDG_DATA_HOME and HOME are unset") {
  const EnvGuard xdg("XDG_DATA_HOME");
  const EnvGuard home("HOME");

  const auto result = corundum::core::user_data_dir("keystone");
  CHECK_FALSE(result.has_value());
}

TEST_CASE("user_data_dir — Linux ignores a relative XDG_DATA_HOME") {
  const EnvGuard xdg("XDG_DATA_HOME", "relative/xdg");
  const EnvGuard home("HOME", "/tmp/testhome");

  const auto result = corundum::core::user_data_dir("keystone");
  REQUIRE(result.has_value());
  CHECK(path_str(*result) == path_str(std::filesystem::path("/tmp/testhome") / ".local" / "share" / "keystone"));
}

TEST_CASE("user_data_dir — Linux errors when HOME is relative") {
  const EnvGuard xdg("XDG_DATA_HOME");
  const EnvGuard home("HOME", "relative/home");

  const auto result = corundum::core::user_data_dir("keystone");
  CHECK_FALSE(result.has_value());
}
#endif

#ifdef _WIN32
TEST_CASE("user_data_dir — Windows prefers %APPDATA%/<app>") {
  const EnvGuard appdata("APPDATA", "C:\\TestAppData");
  const EnvGuard profile("USERPROFILE", "C:\\TestProfile");

  const auto result = corundum::core::user_data_dir("keystone");
  REQUIRE(result.has_value());
  CHECK(path_str(*result) == path_str(std::filesystem::path("C:\\TestAppData") / "keystone"));
}

TEST_CASE("user_data_dir — Windows falls back to %USERPROFILE%/AppData/Roaming/<app>") {
  const EnvGuard appdata("APPDATA");
  const EnvGuard profile("USERPROFILE", "C:\\TestProfile");

  const auto result = corundum::core::user_data_dir("keystone");
  REQUIRE(result.has_value());
  CHECK(path_str(*result) == path_str(std::filesystem::path("C:\\TestProfile") / "AppData" / "Roaming" / "keystone"));
}

TEST_CASE("user_data_dir — Windows errors when both APPDATA and USERPROFILE are unset") {
  const EnvGuard appdata("APPDATA");
  const EnvGuard profile("USERPROFILE");

  const auto result = corundum::core::user_data_dir("keystone");
  CHECK_FALSE(result.has_value());
}

TEST_CASE("user_data_dir — Windows ignores a relative APPDATA") {
  const EnvGuard appdata("APPDATA", "relative\\appdata");
  const EnvGuard profile("USERPROFILE", "C:\\TestProfile");

  const auto result = corundum::core::user_data_dir("keystone");
  REQUIRE(result.has_value());
  CHECK(path_str(*result) == path_str(std::filesystem::path("C:\\TestProfile") / "AppData" / "Roaming" / "keystone"));
}
#endif
