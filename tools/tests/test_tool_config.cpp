// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/toolkit/host/tool_config.hpp>

#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using corundum::toolkit::host::load_tool_config;

namespace {

  fs::path make_temp_dir() {
    static int counter = 0;
    const fs::path path =
        fs::temp_directory_path() / std::format("corundum_tool_config_{}_{}", std::random_device{}(), counter++);
    fs::create_directories(path);
    return path;
  }

  void write_file(const fs::path &path, const std::string &contents) {
    std::ofstream out(path);
    REQUIRE(out.good());
    out << contents;
  }

  /// Arguments plus the mutable argv view load_tool_config() expects.
  class Args {
  public:
    explicit Args(std::vector<std::string> values) : values_(std::move(values)) {
      for (std::string &value : values_)
        argv_.push_back(value.data());
    }

    [[nodiscard]] int argc() const {
      return static_cast<int>(argv_.size());
    }

    [[nodiscard]] char **argv() {
      return argv_.data();
    }

  private:
    std::vector<std::string> values_;
    std::vector<char *> argv_;
  };

  /// Set an environment variable for the test's duration and restore the prior value.
  class ScopedEnv {
  public:
    ScopedEnv(const char *name, const std::string &value) : name_(name) {
      if (const char *prior = std::getenv(name))
        prior_ = prior;
      set(value);
    }

    ~ScopedEnv() {
      if (prior_)
        set(*prior_);
      else
        clear();
    }

    ScopedEnv(const ScopedEnv &) = delete;
    ScopedEnv &operator=(const ScopedEnv &) = delete;
    ScopedEnv(ScopedEnv &&) = delete;
    ScopedEnv &operator=(ScopedEnv &&) = delete;

  private:
    void set(const std::string &value) {
#ifdef _WIN32
      _putenv_s(name_.c_str(), value.c_str());
#else
      // NOLINTNEXTLINE(misc-include-cleaner) — POSIX setenv lives in <stdlib.h>, pulled in via <cstdlib>.
      ::setenv(name_.c_str(), value.c_str(), 1);
#endif
    }

    void clear() {
#ifdef _WIN32
      _putenv_s(name_.c_str(), "");
#else
      // NOLINTNEXTLINE(misc-include-cleaner) — POSIX unsetenv lives in <stdlib.h>, pulled in via <cstdlib>.
      ::unsetenv(name_.c_str());
#endif
    }

    std::string name_;
    std::optional<std::string> prior_;
  };

} // namespace

TEST_CASE("load_tool_config — --config resolves paths against the config's directory") {
  const fs::path dir = make_temp_dir();
  const fs::path config_path = dir / "tools.json";
  write_file(config_path, R"({
    "data_root": "data",
    "dialogue_dir": "dialogue",
    "elevation_step_px": 7.5,
    "tile_diamond_w": 96,
    "tile_diamond_h": 48
  })");

  Args args({"tool", "--config", config_path.string()});
  const auto cfg = load_tool_config(args.argc(), args.argv());
  REQUIRE(cfg.has_value());

  const fs::path config_dir = fs::weakly_canonical(config_path).parent_path();
  CHECK(cfg->data_root == fs::weakly_canonical(config_dir / "data"));
  CHECK(cfg->dialogue_dir == fs::weakly_canonical(config_dir / "dialogue"));
  CHECK(cfg->elevation_step_px == doctest::Approx(7.5f));
  CHECK(cfg->tile_diamond_w == 96);
  CHECK(cfg->tile_diamond_h == 48);

  fs::remove_all(dir);
}

TEST_CASE("load_tool_config — missing fields fall back to defaults") {
  const fs::path dir = make_temp_dir();
  const fs::path config_path = dir / "tools.json";
  write_file(config_path, "{}");

  Args args({"tool", "--config", config_path.string()});
  const auto cfg = load_tool_config(args.argc(), args.argv());
  REQUIRE(cfg.has_value());

  CHECK(cfg->data_root.empty());
  CHECK(cfg->elevation_step_px == doctest::Approx(4.f));
  CHECK(cfg->max_step_height == 4u);
  CHECK(cfg->tile_diamond_w == 128);
  CHECK(cfg->tile_diamond_h == 64);

  fs::remove_all(dir);
}

TEST_CASE("load_tool_config — reports a missing config file") {
  Args args({"tool", "--config", "/corundum/definitely/not/here/tools.json"});
  const auto cfg = load_tool_config(args.argc(), args.argv());

  REQUIRE_FALSE(cfg.has_value());
  CHECK(cfg.error().starts_with("Cannot load tool config"));
}

TEST_CASE("load_tool_config — rejects a non-object JSON document") {
  const fs::path dir = make_temp_dir();
  const fs::path config_path = dir / "tools.json";
  write_file(config_path, "[1, 2, 3]");

  Args args({"tool", "--config", config_path.string()});
  const auto cfg = load_tool_config(args.argc(), args.argv());

  REQUIRE_FALSE(cfg.has_value());
  CHECK(cfg.error() == "Tool config must be a JSON object");

  fs::remove_all(dir);
}

TEST_CASE("load_tool_config — CORUNDUM_TOOLS_CONFIG is used when --config is absent") {
  const fs::path dir = make_temp_dir();
  const fs::path config_path = dir / "tools.json";
  write_file(config_path, R"({"tile_diamond_w": 32})");

  const ScopedEnv env("CORUNDUM_TOOLS_CONFIG", config_path.string());
  Args args({"tool"});
  const auto cfg = load_tool_config(args.argc(), args.argv());

  REQUIRE(cfg.has_value());
  CHECK(cfg->tile_diamond_w == 32);

  fs::remove_all(dir);
}
