#include <doctest/doctest.h>

#include <corundum/core/game_config.hpp>
#include <corundum/render/render_state.hpp>
#include <corundum/render/render_sys.hpp>
#include <corundum/world/map_view.hpp>
#include <corundum/world/pathfinding.hpp>
#include <corundum/world/tilemap/tilemap.hpp>

#include <utility>

namespace {

  namespace render_data = corundum::render;
  using corundum::world::find_path;
  using corundum::world::MapView;
  using corundum::world::tilemap::Tilemap;
  using corundum::world::tilemap::TilemapLayer;

  // A flat chunk_size x chunk_size tilemap: one tile per cell, elevation 0 everywhere.
  Tilemap flat_chunk(int chunk_size) {
    Tilemap tm;
    tm.width = chunk_size;
    tm.height = chunk_size;
    tm.iso_diamond_w = 128;
    tm.iso_diamond_h = 64;
    TilemapLayer layer;
    layer.name = "ground";
    layer.z_index = 0;
    layer.visible = true;
    layer.tiles.assign(static_cast<std::size_t>(chunk_size * chunk_size), 1);
    layer.elevation.assign(static_cast<std::size_t>(chunk_size * chunk_size), 0);
    tm.layers.push_back(std::move(layer));
    return tm;
  }

  void set_elev(Tilemap &tm, int col, int row, uint8_t e) {
    tm.layers[0].elevation[static_cast<std::size_t>(row * tm.width + col)] = e;
  }

  render_data::ChunkEntry make_chunk(int ccol, int crow, Tilemap tm) {
    render_data::ChunkEntry c;
    c.coord = {ccol, crow};
    c.tilemap = std::move(tm);
    return c;
  }

  // World-mode RenderState with a horizontal run of `n` chunks along row 0, starting at (0,0).
  // Each chunk's tilemap is produced by `mk(chunk_index)` so tests can inject elevation walls.
  template <typename Mk> void init_world(render_data::RenderState &state, int chunk_size, int n, Mk &&mk) {
    state.mode = render_data::RenderMode::World;
    state.manifest.chunk_size = chunk_size;
    state.manifest.chunks_wide = n + 2;
    state.manifest.chunks_tall = 3;
    for (int i = 0; i < n; ++i)
      state.chunks.add_active(make_chunk(i, 0, mk(i)));
    state.chunks.set_last_center({0, 0});
    state.chunks.rebuild_slot_table();
    corundum::render::rebuild_collision(state);
    corundum::render::rebuild_world_walkability(state, /*max_step_height=*/4);
  }

  // MapView as build_map_view's world branch wires it (minus the iso math the pathfinder
  // does not use).
  MapView world_map_view(render_data::RenderState &state) {
    MapView m;
    m.collisions = state.agg_collisions.view();
    m.collision_triangles = state.agg_triangles.view();
    m.walkability = &state.agg_walkability;
    m.world_render = &state;
    m.world_w_tiles = static_cast<float>(state.manifest.chunks_wide * state.manifest.chunk_size);
    m.world_h_tiles = static_cast<float>(state.manifest.chunks_tall * state.manifest.chunk_size);
    return m;
  }

} // namespace

TEST_CASE("rebuild_world_walkability — graph spans the active chunk window with correct origin") {
  render_data::RenderState state;
  init_world(state, /*chunk_size=*/8, /*n=*/2, [](int) { return flat_chunk(8); });

  CHECK(state.agg_walkability.col_origin == 0);
  CHECK(state.agg_walkability.row_origin == 0);
  CHECK(state.agg_walkability.width == 16); // 2 chunks * 8
  CHECK(state.agg_walkability.height == 8); // 1 chunk row * 8
}

TEST_CASE("find_path — routes across a chunk boundary in world mode") {
  render_data::RenderState state;
  init_world(state, 8, 2, [](int) { return flat_chunk(8); });
  const MapView map = world_map_view(state);

  // Start in chunk (0,0); goal at global col 12 lives in chunk (1,0).
  const auto path = find_path(map, {2, 4}, {12, 4});
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

  const auto path = find_path(map, {2, 4}, {12, 4});
  CHECK(path.empty()); // global-coord elevation lookup gates the seam edges
}

TEST_CASE("build_map_view — world mode wires up the walkability graph") {
  render_data::RenderState state;
  init_world(state, 8, 1, [](int) { return flat_chunk(8); });

  corundum::core::GameConfig cfg;
  const MapView mv = corundum::world::build_map_view(state, cfg);
  CHECK(mv.walkability == &state.agg_walkability);
  CHECK(mv.elevation_map == nullptr);
}