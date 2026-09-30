// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/core/direction.hpp>

#include <cstdint>

using corundum::core::Direction;

TEST_CASE("direction: opposite is an involution over every direction") {
  for (uint8_t i = 0; i < corundum::core::k_num_directions; ++i) {
    const auto dir = static_cast<Direction>(i);
    CHECK(corundum::core::opposite(corundum::core::opposite(dir)) == dir);
  }
}

TEST_CASE("direction: names round-trip through direction_from_name") {
  for (uint8_t i = 0; i < corundum::core::k_num_directions; ++i) {
    const auto dir = static_cast<Direction>(i);
    CHECK(corundum::core::direction_from_name(corundum::core::direction_name(dir)) == dir);
  }
}

TEST_CASE("direction: unknown and clip-only names resolve to nullopt") {
  CHECK_FALSE(corundum::core::direction_from_name("sideways").has_value());
  CHECK_FALSE(corundum::core::direction_from_name("default").has_value());
}
