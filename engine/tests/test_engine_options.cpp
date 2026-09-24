// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/engine_factory.hpp>

#include <doctest/doctest.h>

TEST_CASE("engine options: defaults when no flags are given") {
  const char *const argv[]{"game"};
  const corundum::EngineOptions options{corundum::parse_engine_args(argv)};

  CHECK(options.config_path == "data/game.json");
  CHECK_FALSE(options.show_debug_hud);
}

TEST_CASE("engine options: --debug enables the HUD") {
  const char *const argv[]{"game", "--debug"};
  const corundum::EngineOptions options{corundum::parse_engine_args(argv)};

  CHECK(options.config_path == "data/game.json");
  CHECK(options.show_debug_hud);
}

TEST_CASE("engine options: unrecognised and null arguments are ignored") {
  const char *const argv[]{"--nope", nullptr, "--debug", ""};
  const corundum::EngineOptions options{corundum::parse_engine_args(argv)};

  CHECK(options.show_debug_hud);
}
