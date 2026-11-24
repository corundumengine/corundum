// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/world/tilemap/tilemap.hpp>

#include <array>
#include <cstddef>

namespace {
struct CellCase {
  int width{};

  int col{};

  int row{};

  std::size_t expected{};
};
} // namespace

TEST_CASE("cell_index: row-major, width cells per row") {
  constexpr std::array k_cases{
      CellCase{.width = 5, .col = 0, .row = 0, .expected = 0},
      CellCase{.width = 5, .col = 4, .row = 0, .expected = 4},
      CellCase{.width = 5, .col = 0, .row = 1, .expected = 5},
      CellCase{.width = 7, .col = 3, .row = 2, .expected = 17},
  };
  for (std::size_t i = 0; i < k_cases.size(); ++i) {
    INFO("case ", i);
    const CellCase &c = k_cases[i];
    CHECK_EQ(corundum::world::tilemap::cell_index(c.width, c.col, c.row), c.expected);
  }
}
