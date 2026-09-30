// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <corundum/render/render_state.hpp>
#include <corundum/render/render_system.hpp>
#include <corundum/world/map_view.hpp>
#include <corundum/world/tilemap/tilemap.hpp>

#include <cstddef>
#include <utility>

/// Chunked-world fixtures shared by the world pathfinding and render walkability
/// tests: both build the same RenderState and wire it into a MapView. Every
/// helper is `inline` so the two translation units share one definition.
namespace corundum::test {

  /// A flat chunk_size x chunk_size tilemap: one tile per cell, elevation 0 everywhere.
  inline world::tilemap::Tilemap flat_chunk(int chunk_size) {
    using world::tilemap::Tilemap;
    using world::tilemap::TilemapLayer;
    Tilemap tm;
    tm.width = chunk_size;
    tm.height = chunk_size;
    tm.iso_diamond_w = 128;
    tm.iso_diamond_h = 64;
    TilemapLayer layer;
    layer.name = "ground";
    layer.z_index = 0;
    layer.visible = true;
    layer.tiles.assign(static_cast<std::size_t>(chunk_size) * static_cast<std::size_t>(chunk_size), 1);
    layer.elevation.assign(static_cast<std::size_t>(chunk_size) * static_cast<std::size_t>(chunk_size), 0);
    tm.layers.push_back(std::move(layer));
    return tm;
  }

  inline render::ChunkEntry make_chunk(int ccol, int crow, world::tilemap::Tilemap tm) {
    render::ChunkEntry c;
    c.coord = {.col = ccol, .row = crow};
    c.tilemap = std::move(tm);
    return c;
  }

  /// World-mode RenderState with a horizontal run of `n` chunks along row 0, starting at (0,0).
  /// Each chunk's tilemap is produced by `mk(chunk_index)` so tests can inject elevation walls.
  template <typename Mk> void init_world(render::RenderState &state, int chunk_size, int n, const Mk &mk) {
    state.mode = render::RenderMode::World;
    state.manifest.chunk_size = chunk_size;
    state.manifest.chunks_wide = n + 2;
    state.manifest.chunks_tall = 3;
    for (int i = 0; i < n; ++i)
      state.chunks.add_active(make_chunk(i, 0, mk(i)));
    state.chunks.set_last_center({.col = 0, .row = 0});
    render::rebuild_world_aggregates(state, /*max_step_height=*/4);
  }

  /// MapView as build_map_view's world branch wires it (minus the iso math the pathfinder
  /// does not use).
  inline world::MapView world_map_view(const render::RenderState &state) {
    world::MapView m;
    m.collisions = state.agg_collisions.view();
    m.collision_triangles = state.agg_triangles.view();
    m.walkability = &state.agg_walkability;
    m.world_render = &state;
    m.world_w_tiles = static_cast<float>(state.manifest.chunks_wide * state.manifest.chunk_size);
    m.world_h_tiles = static_cast<float>(state.manifest.chunks_tall * state.manifest.chunk_size);
    return m;
  }

} // namespace corundum::test
