// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/render/render_state.hpp>

#include "world_test_fixtures.hpp"

namespace {

  using corundum::test::flat_chunk;
  using corundum::test::init_world;

} // namespace

TEST_CASE("rebuild_world_walkability — graph spans the active chunk window with correct origin") {
  corundum::render::RenderState state;
  init_world(state, /*chunk_size=*/8, /*n=*/2, [](int) { return flat_chunk(8); });

  CHECK(state.agg_walkability.col_origin == 0);
  CHECK(state.agg_walkability.row_origin == 0);
  CHECK(state.agg_walkability.width == 16); // 2 chunks * 8
  CHECK(state.agg_walkability.height == 8); // 1 chunk row * 8
}
