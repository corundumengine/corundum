// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/render/chunk_window.hpp>
#include <corundum/render/render_state.hpp>
#include <corundum/render/world_aggregates.hpp>
#include <corundum/world/tilemap/tilemap.hpp>
#include <utility>

namespace tilemap = corundum::world::tilemap;
namespace render_data = corundum::render;
namespace render_system = corundum::render;

TEST_CASE("rebuild_collision — world mode aggregates rects across chunks with tile-unit offsets") {
  render_data::RenderState state;
  auto &world_area = state.area.emplace<render_data::WorldArea>();
  world_area.manifest.chunk_size = 16;
  world_area.manifest.chunks_wide = 4;
  world_area.manifest.chunks_tall = 4;

  // Chunk (0,0): one rect at (col=3, row=4) — origin chunk, no offset.
  render_data::ChunkEntry chunk00;
  chunk00.coord = {.col = 0, .row = 0};
  chunk00.tilemap.iso_diamond_w = 128;
  chunk00.tilemap.collisions.push_back(3.f, 4.f, 2.f, 1.f, /*elevation=*/0);
  world_area.chunks.add_active(std::move(chunk00));

  // Chunk (1,0): one rect at (col=5, row=6) and one triangle at (col=7, row=8).
  // Before the fix, offsets are applied in pixel units (chunk_size * tile_px * tile_scale),
  // pushing these rects ~2048 columns away from where they belong.
  render_data::ChunkEntry chunk10;
  chunk10.coord = {.col = 1, .row = 0};
  chunk10.tilemap.iso_diamond_w = 128;
  chunk10.tilemap.collisions.push_back(5.f, 6.f, 1.f, 2.f, /*elevation=*/1);
  chunk10.tilemap.collision_triangles.push_back(7.f, 8.f, 1.f, 1.f, tilemap::TriangleCut::NorthWest, /*elevation=*/0);
  world_area.chunks.add_active(std::move(chunk10));

  render_system::rebuild_collision(world_area);

  const auto &collisions = world_area.aggregates.collisions;
  REQUIRE_EQ(collisions.size(), 2);
  // (0,0) — no offset.
  CHECK_EQ(collisions.cols[0], 3.f);
  CHECK_EQ(collisions.rows[0], 4.f);
  CHECK_EQ(collisions.col_spans[0], 2.f);
  CHECK_EQ(collisions.row_spans[0], 1.f);
  CHECK_EQ(collisions.elevations[0], 0);
  // (1,0) — offset by exactly chunk_size (tile units), not chunk_size * tile_px * tile_scale.
  CHECK_EQ(collisions.cols[1], doctest::Approx(5.f + 16.f));
  CHECK_EQ(collisions.rows[1], doctest::Approx(6.f));
  CHECK_EQ(collisions.col_spans[1], 1.f);
  CHECK_EQ(collisions.row_spans[1], 2.f);
  CHECK_EQ(collisions.elevations[1], 1);

  const auto &triangles = world_area.aggregates.triangles;
  REQUIRE_EQ(triangles.size(), 1);
  CHECK_EQ(triangles.cols[0], doctest::Approx(7.f + 16.f));
  CHECK_EQ(triangles.rows[0], doctest::Approx(8.f));
  CHECK_EQ(triangles.cuts[0], tilemap::TriangleCut::NorthWest);
}

TEST_CASE("rebuild_collision — world mode: vertically adjacent chunk offsets by row, not col") {
  render_data::RenderState state;
  auto &world_area = state.area.emplace<render_data::WorldArea>();
  world_area.manifest.chunk_size = 8;
  world_area.manifest.chunks_wide = 4;
  world_area.manifest.chunks_tall = 4;

  render_data::ChunkEntry chunk02;
  chunk02.coord = {.col = 0, .row = 2};
  chunk02.tilemap.iso_diamond_w = 64;
  chunk02.tilemap.collisions.push_back(1.f, 2.f, 1.f, 1.f, 0);
  world_area.chunks.add_active(std::move(chunk02));

  render_system::rebuild_collision(world_area);

  const auto &collisions = world_area.aggregates.collisions;
  REQUIRE_EQ(collisions.size(), 1);
  // Row offset only — col must be unchanged.
  CHECK_EQ(collisions.cols[0], 1.f);
  CHECK_EQ(collisions.rows[0], doctest::Approx(2.f + (2.f * 8.f)));
}

TEST_CASE("rebuild_collision — empty active_chunks is a no-op") {
  render_data::RenderState state;
  auto &world_area = state.area.emplace<render_data::WorldArea>();
  world_area.manifest.chunk_size = 16;
  // Pre-populate to confirm it's cleared (or left empty) and not touched.
  world_area.aggregates.collisions.push_back(99.f, 99.f, 1.f, 1.f, 0);

  render_system::rebuild_collision(world_area);

  CHECK_EQ(world_area.aggregates.collisions.size(), 0);
  CHECK_EQ(world_area.aggregates.triangles.size(), 0);
}
