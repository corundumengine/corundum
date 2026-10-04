// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

// Compile-time guard for the gameplay umbrella surface: if a re-exported name in
// corundum/gameplay/gameplay.hpp is renamed or dropped, this TU stops compiling.

#include <corundum/gameplay/gameplay.hpp>
#include <doctest/doctest.h>

#include <expected>
#include <memory>
#include <string>
#include <type_traits>

static_assert(std::is_same_v<corundum::gameplay::EventAction, corundum::gameplay::dialogue::EventAction>);
static_assert(std::is_same_v<decltype(&corundum::gameplay::make_runtime),
                             std::expected<std::unique_ptr<corundum::gameplay::Runtime>, std::string> (*)(
                                 const corundum::EngineOptions &)>);

TEST_CASE("gameplay umbrella re-exports the game-facing surface") {
  corundum::Engine engine{};

  const corundum::gameplay::EventAction action{.name = "set_flag", .args = {"example"}};
  corundum::set_flag(engine.flags, action.args[0]);

  CHECK(engine.flags.contains("example"));
}
