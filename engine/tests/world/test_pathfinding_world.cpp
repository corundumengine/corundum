// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <doctest/doctest.h>

#include <corundum/core/game_config.hpp>
#include <corundum/render/render_state.hpp>
#include <corundum/world/map_view.hpp>
#include <corundum/world/pathfinding.hpp>
#include <corundum/world/tilemap/tilemap.hpp>

#include "world_test_fixtures.hpp"

namespace {

  namespace render_data = corundum::render;
  using corundum::test::flat_chunk;
  using corundum::test::init_world;
  using corundum::test::world_map_view;
  using corundum::world::find_path;
  using corundum::world::MapView;
  using corundum::world::tilemap::Tilemap;

  // Row-major index into a per-cell tilemap array.
  std::size_t cell_index(int col, int row, int width) {
    return (static_cast<std::size_t>(row) * static_cast<std::size_t>(width)) + static_cast<std::size_t>(col);
  }

  void set_elev(Tilemap &tm, int col, int row, uint8_t e) {
    tm.layers[0].elevation[cell_index(col, row, tm.width)] = e;
  }

} // namespace

TEST_CASE("find_path — routes across a chunk boundary in world mode") {
  render_data::RenderState state;
  init_world(state, 8, 2, [](int) { return flat_chunk(8); });
  const MapView map = world_map_view(state);

  // Start in chunk (0,0); goal at global col 12 lives in chunk (1,0).
  const auto path = find_path(map, {.col = 2, .row = 4}, {.col = 12, .row = 4});
  REQUIRE_FALSE(path.empty());
  CHECK(path.back().col == 12);
  CHECK(path.back().row == 4);
}

TEST_CASE("find_path — an elevation wall on the chunk seam blocks the route") {
  render_data::RenderState state;
  init_world(state, 8, 2, [](int i) {
    Tilemap tm = flat_chunk(8);
    if (i == 1) // seam column: chunk (1,0) local col 0 == global col 8
      for (int row = 0; row < 8; ++row)
        set_elev(tm, 0, row, 50);
    return tm;
  });
  const MapView map = world_map_view(state);

  const auto path = find_path(map, {.col = 2, .row = 4}, {.col = 12, .row = 4});
  CHECK(path.empty()); // global-coord elevation lookup gates the seam edges
}

TEST_CASE("build_map_view — world mode wires up the walkability graph") {
  render_data::RenderState state;
  init_world(state, 8, 1, [](int) { return flat_chunk(8); });

  const corundum::core::GameConfig cfg;
  const MapView mv = corundum::world::build_map_view(state, cfg);
  CHECK(mv.walkability == &state.agg_walkability);
  CHECK(mv.elevation_map == nullptr);
}
