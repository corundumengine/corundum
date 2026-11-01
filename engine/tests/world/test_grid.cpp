// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/world/grid.hpp>
#include <corundum/world/picking.hpp>

namespace {

  using corundum::world::tile_distance;
  using corundum::world::tile_of;
  using corundum::world::TileCoord;

} // namespace

TEST_CASE("grid: tile_of puts a whole position on its own tile") {
  CHECK_EQ(tile_of(3.f, 7.f), TileCoord{.col = 3, .row = 7});
}

TEST_CASE("grid: tile_of puts a mid-step position on the tile under the feet") {
  CHECK_EQ(tile_of(3.6f, 7.4f), TileCoord{.col = 4, .row = 7});
}

TEST_CASE("grid: tile_of puts a negative position on the tile under the feet") {
  CHECK_EQ(tile_of(-0.6f, -0.4f), TileCoord{.col = -1, .row = 0});
}

TEST_CASE("grid: tile_distance counts a diagonal step as one") {
  CHECK_EQ(tile_distance({.col = 2, .row = 3}, {.col = 3, .row = 4}), 1);
}

TEST_CASE("grid: tile_distance is the larger of the two axis gaps") {
  CHECK_EQ(tile_distance({.col = 2, .row = 3}, {.col = 7, .row = 5}), 5);
}

TEST_CASE("grid: tile_distance from a tile to itself is 0") {
  CHECK_EQ(tile_distance({.col = 4, .row = 4}, {.col = 4, .row = 4}), 0);
}
