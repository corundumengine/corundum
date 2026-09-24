// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include "render_scale.hpp"

using corundum::platform::glfw::derive_screen_scale;
using corundum::platform::glfw::physical_font_size;
using corundum::platform::glfw::to_logical;

TEST_CASE("derive_screen_scale: identity at 1x and doubled at 2x") {
  const auto one = derive_screen_scale(1000, 800, 1000, 800);
  CHECK(one.x == doctest::Approx(1.f));
  CHECK(one.y == doctest::Approx(1.f));

  const auto two = derive_screen_scale(2000, 1600, 1000, 800);
  CHECK(two.x == doctest::Approx(2.f));
  CHECK(two.y == doctest::Approx(2.f));
}

TEST_CASE("derive_screen_scale: fractional content scale") {
  const auto scaled = derive_screen_scale(1500, 1200, 1000, 800);
  CHECK(scaled.x == doctest::Approx(1.5f));
  CHECK(scaled.y == doctest::Approx(1.5f));
}

TEST_CASE("derive_screen_scale: degenerate logical size falls back to 1") {
  const auto degenerate = derive_screen_scale(0, 0, 0, 0);
  CHECK(degenerate.x == doctest::Approx(1.f));
  CHECK(degenerate.y == doctest::Approx(1.f));
}

TEST_CASE("physical_font_size rounds to the nearest physical pixel") {
  CHECK(physical_font_size(16, 2.f) == 32u);
  CHECK(physical_font_size(16, 1.5f) == 24u);
  CHECK(physical_font_size(16, 1.f) == 16u);
  CHECK(physical_font_size(15, 1.5f) == 23u); // round-half-away-from-zero
}

TEST_CASE("physical metrics round-trip back to logical") {
  CHECK(to_logical(32.f, 2.f) == doctest::Approx(16.f));
  CHECK(to_logical(24.f, 1.5f) == doctest::Approx(16.f));
  CHECK(to_logical(16.f, 1.f) == doctest::Approx(16.f));
}
