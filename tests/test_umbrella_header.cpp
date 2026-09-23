// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

// Compile-time guard for the game-facing surface: if a re-exported name in
// corundum/corundum.hpp is renamed or dropped, this TU stops compiling.

#include <corundum/corundum.hpp>
#include <doctest/doctest.h>

#include <expected>
#include <span>
#include <string>
#include <type_traits>

static_assert(
    std::is_same_v<decltype(&corundum::parse_engine_args), corundum::EngineOptions (*)(std::span<const char *const>)>);
static_assert(std::is_same_v<decltype(&corundum::make_engine),
                             std::expected<corundum::Engine, std::string> (*)(const corundum::EngineOptions &)>);

TEST_CASE("umbrella header re-exports the game-facing surface") {
  const corundum::EngineOptions options{};
  corundum::Engine engine{};
  const corundum::EventAction action{.name = "set_flag", .args = {"example"}};

  corundum::set_flag(engine.flags, action.args[0]);

  CHECK(options.config_path == "data/game.json");
  CHECK(engine.flags.contains("example"));
}
