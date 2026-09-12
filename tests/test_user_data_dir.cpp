// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/platform/paths.hpp>

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace {

  // Saves the current value of an environment variable on construction and
  // restores it (or removes it, if it was absent) on destruction, so tests can
  // mutate process env vars without leaking state into later test cases.
  struct EnvGuard {
    std::string name;
    std::optional<std::string> saved;

    explicit EnvGuard(std::string_view variable_name) : name(variable_name) {
      const char *const value = std::getenv(name.c_str());
      if (value != nullptr)
        saved = value;
    }

    ~EnvGuard() {
      if (saved)
        set_env(name.c_str(), saved->c_str());
      else
        unset_env(name.c_str());
    }

    EnvGuard(const EnvGuard &) = delete;
    EnvGuard &operator=(const EnvGuard &) = delete;

    void set(const char *value) {
      set_env(name.c_str(), value);
    }

    void clear() {
      unset_env(name.c_str());
    }

  private:
    static void set_env(const char *variable_name, const char *value) {
#if defined(_WIN32)
      _putenv_s(variable_name, value);
#else
      setenv(variable_name, value, 1);
#endif
    }

    static void unset_env(const char *variable_name) {
#if defined(_WIN32)
      _putenv_s(variable_name, "");
#else
      unsetenv(variable_name);
#endif
    }
  };

  [[nodiscard]] std::string path_str(const std::filesystem::path &p) {
    return p.string();
  }

} // namespace

TEST_CASE("user_data_dir — empty app_name is rejected") {
  const auto result = corundum::platform::user_data_dir("");
  CHECK_FALSE(result.has_value());
}

#if defined(__APPLE__)
TEST_CASE("user_data_dir — macOS resolves $HOME/Library/Application Support/<app>") {
  EnvGuard home("HOME");
  home.set("/tmp/testhome");

  const auto result = corundum::platform::user_data_dir("keystone");
  REQUIRE(result.has_value());
  CHECK(path_str(*result) ==
        path_str(std::filesystem::path("/tmp/testhome") / "Library" / "Application Support" / "keystone"));
}

TEST_CASE("user_data_dir — macOS errors when HOME is unset") {
  EnvGuard home("HOME");
  home.clear();

  const auto result = corundum::platform::user_data_dir("keystone");
  CHECK_FALSE(result.has_value());
}
#endif

#if defined(__linux__)
TEST_CASE("user_data_dir — Linux prefers $XDG_DATA_HOME/<app>") {
  EnvGuard xdg("XDG_DATA_HOME");
  EnvGuard home("HOME");
  xdg.set("/tmp/testxdg");
  home.set("/tmp/testhome");

  const auto result = corundum::platform::user_data_dir("keystone");
  REQUIRE(result.has_value());
  CHECK(path_str(*result) == path_str(std::filesystem::path("/tmp/testxdg") / "keystone"));
}

TEST_CASE("user_data_dir — Linux falls back to $HOME/.local/share/<app>") {
  EnvGuard xdg("XDG_DATA_HOME");
  EnvGuard home("HOME");
  xdg.clear();
  home.set("/tmp/testhome");

  const auto result = corundum::platform::user_data_dir("keystone");
  REQUIRE(result.has_value());
  CHECK(path_str(*result) == path_str(std::filesystem::path("/tmp/testhome") / ".local" / "share" / "keystone"));
}

TEST_CASE("user_data_dir — Linux errors when both XDG_DATA_HOME and HOME are unset") {
  EnvGuard xdg("XDG_DATA_HOME");
  EnvGuard home("HOME");
  xdg.clear();
  home.clear();

  const auto result = corundum::platform::user_data_dir("keystone");
  CHECK_FALSE(result.has_value());
}
#endif

#if defined(_WIN32)
TEST_CASE("user_data_dir — Windows prefers %APPDATA%/<app>") {
  EnvGuard appdata("APPDATA");
  EnvGuard profile("USERPROFILE");
  appdata.set("C:\\TestAppData");
  profile.set("C:\\TestProfile");

  const auto result = corundum::platform::user_data_dir("keystone");
  REQUIRE(result.has_value());
  CHECK(path_str(*result) == path_str(std::filesystem::path("C:\\TestAppData") / "keystone"));
}

TEST_CASE("user_data_dir — Windows falls back to %USERPROFILE%/AppData/Roaming/<app>") {
  EnvGuard appdata("APPDATA");
  EnvGuard profile("USERPROFILE");
  appdata.clear();
  profile.set("C:\\TestProfile");

  const auto result = corundum::platform::user_data_dir("keystone");
  REQUIRE(result.has_value());
  CHECK(path_str(*result) == path_str(std::filesystem::path("C:\\TestProfile") / "AppData" / "Roaming" / "keystone"));
}

TEST_CASE("user_data_dir — Windows errors when both APPDATA and USERPROFILE are unset") {
  EnvGuard appdata("APPDATA");
  EnvGuard profile("USERPROFILE");
  appdata.clear();
  profile.clear();

  const auto result = corundum::platform::user_data_dir("keystone");
  CHECK_FALSE(result.has_value());
}
#endif
