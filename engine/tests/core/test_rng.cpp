// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/core/rng.hpp>

#include <array>
#include <cstdint>

using corundum::core::Rng;

TEST_CASE("Rng — pcg32 reference vector for seed 42, stream 54") {
  Rng rng{42, 54};

  CHECK(rng.next_u32() == 0xa15c02b7U);
  CHECK(rng.next_u32() == 0x7b47f409U);
  CHECK(rng.next_u32() == 0xba1d3330U);
  CHECK(rng.next_u32() == 0x83d2f293U);
  CHECK(rng.next_u32() == 0xbfa4784bU);
  CHECK(rng.next_u32() == 0xcbed606eU);
}

TEST_CASE("Rng — the same seed and stream produce identical sequences") {
  Rng a{7, 1};
  Rng b{7, 1};

  for (int i = 0; i < 1000; ++i)
    CHECK(a.next_u32() == b.next_u32());
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): CHECK expands to branches.
TEST_CASE("Rng — range stays in bounds and reaches both endpoints") {
  Rng rng{1234, 99};
  bool saw_low = false;
  bool saw_high = false;

  for (int i = 0; i < 10000; ++i) {
    const std::int32_t value = rng.range(-3, 3);
    CHECK(value >= -3);
    CHECK(value <= 3);
    saw_low = saw_low || value == -3;
    saw_high = saw_high || value == 3;
  }

  CHECK(saw_low);
  CHECK(saw_high);
}

TEST_CASE("Rng — state round-trips") {
  Rng rng{555, 3};
  for (int i = 0; i < 10; ++i)
    static_cast<void>(rng.next_u32());

  const Rng::State saved = rng.state();

  std::array<std::uint32_t, 5> expected{};
  for (auto &value : expected)
    value = rng.next_u32();

  rng.set_state(saved);
  for (const std::uint32_t value : expected)
    CHECK(rng.next_u32() == value);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): CHECK expands to branches.
TEST_CASE("Rng — chance clamps at the boundaries and unit stays below one") {
  Rng rng{9, 9};

  for (int i = 0; i < 1000; ++i) {
    CHECK_FALSE(rng.chance(0.f));
    CHECK(rng.chance(1.f));
    const float u = rng.unit();
    CHECK(u >= 0.f);
    CHECK(u < 1.f);
  }
}
