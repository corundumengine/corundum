// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include "gamepad_mappings.hpp"

#include <array>
#include <format>
#include <string_view>

namespace {

  constexpr std::string_view k_first = "first,a:b0,platform:Mac OS X,";
  constexpr std::string_view k_second = "second,b:b0,platform:Mac OS X,";

} // namespace

TEST_CASE("gamepad mappings: rows join with newlines in order") {
  const std::array<std::string_view, 2> rows{k_first, k_second};
  CHECK(corundum::platform::glfw::join_gamepad_mappings(rows) == std::format("{}\n{}\n", k_first, k_second));
}

TEST_CASE("gamepad mappings: no rows join to an empty string") {
  const std::array<std::string_view, 0> empty{};
  CHECK(corundum::platform::glfw::join_gamepad_mappings(empty).empty());
}
