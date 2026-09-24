// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include "texture_byte_size.hpp"

#include <cstddef>

using corundum::platform::glfw::rgba8_byte_count;

TEST_CASE("rgba8_byte_count: four bytes per pixel") {
  CHECK(rgba8_byte_count(1, 1) == 4);
  CHECK(rgba8_byte_count(64, 32) == (std::size_t{64} * 32 * 4));
}

TEST_CASE("rgba8_byte_count: dimensions whose int product would overflow") {
  // 32768 x 32768 x 4 exceeds INT_MAX, so the multiply must be widened before it happens.
  CHECK(rgba8_byte_count(32768, 32768) == (std::size_t{1} << 32u));
}

TEST_CASE("rgba8_byte_count: zero on a degenerate dimension") {
  CHECK(rgba8_byte_count(0, 512) == 0);
  CHECK(rgba8_byte_count(512, 0) == 0);
}
