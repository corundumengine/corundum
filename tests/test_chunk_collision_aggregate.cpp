#include <doctest/doctest.h>

#include <corundum/gameplay/world/tilemap/tilemap.hpp>
#include <corundum/render/data/render_state.hpp>
#include <corundum/render/sys/render_sys.hpp>

namespace tilemap = corundum::gameplay::world::tilemap;
namespace render_data = corundum::render::data;
namespace render_sys = corundum::render::sys;

TEST_CASE("rebuild_collision — world mode aggregates rects across chunks with tile-unit offsets") {
  render_data::RenderState state;
  state.mode = render_data::RenderMode::World;
  state.manifest.chunk_size = 16;
  state.manifest.chunks_wide = 4;
  state.manifest.chunks_tall = 4;

  // Chunk (0,0): one rect at (col=3, row=4) — origin chunk, no offset.
  render_data::ChunkEntry chunk00;
  chunk00.coord = {0, 0};
  chunk00.tilemap.iso_diamond_w = 128;
  chunk00.tilemap.collisions.push_back(3.f, 4.f, 2.f, 1.f, /*elevation=*/0);
  state.chunks.add_active(std::move(chunk00));

  // Chunk (1,0): one rect at (col=5, row=6) and one triangle at (col=7, row=8).
  // Before the fix, offsets are applied in pixel units (chunk_size * tile_px * tile_scale),
  // pushing these rects ~2048 columns away from where they belong.
  render_data::ChunkEntry chunk10;
  chunk10.coord = {1, 0};
  chunk10.tilemap.iso_diamond_w = 128;
  chunk10.tilemap.collisions.push_back(5.f, 6.f, 1.f, 2.f, /*elevation=*/1);
  chunk10.tilemap.collision_triangles.push_back(7.f, 8.f, 1.f, 1.f, tilemap::TriangleCut::NorthWest, /*elevation=*/0);
  state.chunks.add_active(std::move(chunk10));

  render_sys::rebuild_collision(state);

  REQUIRE(state.agg_collisions.size() == 2);
  // (0,0) — no offset.
  CHECK(state.agg_collisions.cols[0] == 3.f);
  CHECK(state.agg_collisions.rows[0] == 4.f);
  CHECK(state.agg_collisions.col_spans[0] == 2.f);
  CHECK(state.agg_collisions.row_spans[0] == 1.f);
  CHECK(state.agg_collisions.elevations[0] == 0);
  // (1,0) — offset by exactly chunk_size (tile units), not chunk_size * tile_px * tile_scale.
  CHECK(state.agg_collisions.cols[1] == doctest::Approx(5.f + 16.f));
  CHECK(state.agg_collisions.rows[1] == doctest::Approx(6.f));
  CHECK(state.agg_collisions.col_spans[1] == 1.f);
  CHECK(state.agg_collisions.row_spans[1] == 2.f);
  CHECK(state.agg_collisions.elevations[1] == 1);

  REQUIRE(state.agg_triangles.size() == 1);
  CHECK(state.agg_triangles.cols[0] == doctest::Approx(7.f + 16.f));
  CHECK(state.agg_triangles.rows[0] == doctest::Approx(8.f));
  CHECK(state.agg_triangles.cuts[0] == tilemap::TriangleCut::NorthWest);
}

TEST_CASE("rebuild_collision — world mode: vertically adjacent chunk offsets by row, not col") {
  render_data::RenderState state;
  state.mode = render_data::RenderMode::World;
  state.manifest.chunk_size = 8;
  state.manifest.chunks_wide = 4;
  state.manifest.chunks_tall = 4;

  render_data::ChunkEntry chunk02;
  chunk02.coord = {0, 2};
  chunk02.tilemap.iso_diamond_w = 64;
  chunk02.tilemap.collisions.push_back(1.f, 2.f, 1.f, 1.f, 0);
  state.chunks.add_active(std::move(chunk02));

  render_sys::rebuild_collision(state);

  REQUIRE(state.agg_collisions.size() == 1);
  // Row offset only — col must be unchanged.
  CHECK(state.agg_collisions.cols[0] == 1.f);
  CHECK(state.agg_collisions.rows[0] == doctest::Approx(2.f + 2.f * 8.f));
}

TEST_CASE("rebuild_collision — empty active_chunks is a no-op") {
  render_data::RenderState state;
  state.mode = render_data::RenderMode::World;
  state.manifest.chunk_size = 16;
  // Pre-populate to confirm it's cleared (or left empty) and not touched.
  state.agg_collisions.push_back(99.f, 99.f, 1.f, 1.f, 0);

  render_sys::rebuild_collision(state);

  CHECK(state.agg_collisions.size() == 0);
  CHECK(state.agg_triangles.size() == 0);
}
