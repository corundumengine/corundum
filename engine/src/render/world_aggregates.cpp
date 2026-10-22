// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/render/world_aggregates.hpp>

#include <corundum/render/render_state.hpp>

#include <corundum/world/portals/portal.hpp>
#include <corundum/world/tilemap/tilemap.hpp>
#include <corundum/world/tilemap/walkability.hpp>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

namespace corundum::render {

  void rebuild_collision(WorldArea &area) {
    using namespace corundum::world::tilemap;
    area.aggregates.collisions = {};
    area.aggregates.triangles = {};
    if (area.chunks.active_empty())
      return;

    for (const auto &entry : area.chunks.active()) {
      const float ox = static_cast<float>(entry.coord.col * area.manifest.chunk_size);
      const float oy = static_cast<float>(entry.coord.row * area.manifest.chunk_size);
      const auto &cr = entry.tilemap.collisions;
      for (std::size_t i = 0; i < cr.size(); ++i)
        area.aggregates.collisions.push_back(cr.cols[i] + ox, cr.rows[i] + oy, cr.col_spans[i], cr.row_spans[i],
                                             cr.elevations[i]);
      const auto &ct = entry.tilemap.collision_triangles;
      for (std::size_t i = 0; i < ct.size(); ++i)
        area.aggregates.triangles.push_back(ct.cols[i] + ox, ct.rows[i] + oy, ct.col_spans[i], ct.row_spans[i],
                                            ct.cuts[i], ct.elevations[i]);
    }
  }

  namespace {

    /// Rebuild @c area.aggregates.portals from the active chunks, offsetting each chunk-local
    /// portal (and its chunk-target spawn) into global world tile coordinates.
    void rebuild_world_portals(WorldArea &area) {
      area.aggregates.portals.clear();
      const int chunk_size = area.manifest.chunk_size;
      for (const auto &chunk : area.chunks.active()) {
        const int ox{chunk.coord.col * chunk_size};
        const int oy{chunk.coord.row * chunk_size};
        for (const auto &portal : chunk.portals) {
          area.aggregates.portals.push_back(portal);
          corundum::world::Portal &agg = area.aggregates.portals.back();
          agg.col += static_cast<float>(ox);
          agg.row += static_cast<float>(oy);
          if (agg.target_chunk_col >= 0 && agg.target_chunk_row >= 0) {
            agg.spawn_col += agg.target_chunk_col * chunk_size;
            agg.spawn_row += agg.target_chunk_row * chunk_size;
          }
        }
      }
    }

    /// Active chunk owning the global tile cell (gc, gr), or nullptr if outside the window.
    const render::ChunkEntry *chunk_owner_at(const ChunkWindow &chunks, int gc, int gr, int chunk_size) noexcept {
      const int cc = static_cast<int>(std::floor(static_cast<float>(gc) / static_cast<float>(chunk_size)));
      const int cr = static_cast<int>(std::floor(static_cast<float>(gr) / static_cast<float>(chunk_size)));
      const std::optional<std::size_t> slot =
          chunks.slot_at_offset(cc - chunks.last_center().col, cr - chunks.last_center().row);
      if (!slot)
        return nullptr;
      return &chunks.active_at(*slot);
    }

    /// Elevation of the global tile cell (gc, gr); 0 when no active chunk owns it.
    int chunk_tile_elevation_at(const ChunkWindow &chunks, int gc, int gr, int chunk_size) noexcept {
      using corundum::world::tilemap::elevation_at;
      const render::ChunkEntry *e = chunk_owner_at(chunks, gc, gr, chunk_size);
      return e != nullptr ? elevation_at(e->tilemap, gc - (e->coord.col * chunk_size), gr - (e->coord.row * chunk_size))
                          : 0;
    }

    /// First walkability pass: clear the edge from each cell to any neighbour whose
    /// elevation differs by more than @p max_step_height.
    void clear_steep_walkability_edges(const ChunkWindow &chunks, corundum::world::tilemap::WalkabilityGraph &g,
                                       int chunk_size, int max_step_height) {
      using corundum::world::tilemap::k_walk_neighbors;
      using corundum::world::tilemap::walk_dir_bit;
      for (int r = 0; r < g.height; ++r) {
        for (int c = 0; c < g.width; ++c) {
          const int gc = c + g.col_origin;
          const int gr = r + g.row_origin;
          const int e0 = chunk_tile_elevation_at(chunks, gc, gr, chunk_size);
          const std::size_t idx =
              (static_cast<std::size_t>(r) * static_cast<std::size_t>(g.width)) + static_cast<std::size_t>(c);
          for (const auto &[delta_col, delta_row] : k_walk_neighbors) {
            const int nc = c + delta_col;
            const int nr = r + delta_row;
            if (nc < 0 || nr < 0 || nc >= g.width || nr >= g.height)
              continue;
            if (std::abs(e0 - chunk_tile_elevation_at(chunks, gc + delta_col, gr + delta_row, chunk_size)) >
                max_step_height)
              g.edges[idx] &= static_cast<uint8_t>(~walk_dir_bit(delta_col, delta_row));
          }
        }
      }
    }

    /// Second walkability pass: ramp cells force both directions along their axis back open
    /// (mirrors build_walkability_graph's second pass). set_passable() takes global coords.
    void reopen_walkability_ramp_edges(const ChunkWindow &chunks, corundum::world::tilemap::WalkabilityGraph &g,
                                       int chunk_size) {
      using corundum::world::tilemap::ramp_axis_at;
      using corundum::world::tilemap::RampAxis;
      for (int r = 0; r < g.height; ++r) {
        for (int c = 0; c < g.width; ++c) {
          const int gc = c + g.col_origin;
          const int gr = r + g.row_origin;
          const render::ChunkEntry *e = chunk_owner_at(chunks, gc, gr, chunk_size);
          if (e == nullptr)
            continue;
          const std::optional<RampAxis> axis =
              ramp_axis_at(e->tilemap, gc - (e->coord.col * chunk_size), gr - (e->coord.row * chunk_size));
          if (!axis)
            continue;
          const auto [dc0, dr0] = *axis == RampAxis::NorthSouth ? std::pair{0, -1} : std::pair{1, 0};
          g.set_passable(gc, gr, gc + dc0, gr + dr0, true);
          g.set_passable(gc, gr, gc - dc0, gr - dr0, true);
        }
      }
    }

  } // namespace

  void rebuild_world_walkability(WorldArea &area, int max_step_height) {
    using corundum::world::tilemap::WalkabilityGraph;

    area.aggregates.walkability = {};
    if (area.chunks.active_empty())
      return;
    const int chunk_size = area.manifest.chunk_size;
    if (chunk_size <= 0)
      return;

    int min_cc = INT_MAX;
    int min_cr = INT_MAX;
    int max_cc = INT_MIN;
    int max_cr = INT_MIN;
    for (const auto &e : area.chunks.active()) {
      min_cc = std::min(min_cc, e.coord.col);
      max_cc = std::max(max_cc, e.coord.col);
      min_cr = std::min(min_cr, e.coord.row);
      max_cr = std::max(max_cr, e.coord.row);
    }

    WalkabilityGraph &g = area.aggregates.walkability;
    g.col_origin = min_cc * chunk_size;
    g.row_origin = min_cr * chunk_size;
    g.width = (max_cc - min_cc + 1) * chunk_size;
    g.height = (max_cr - min_cr + 1) * chunk_size;
    g.edges.assign(static_cast<std::size_t>(g.width) * static_cast<std::size_t>(g.height),
                   corundum::world::tilemap::k_all_walk_dirs);

    clear_steep_walkability_edges(area.chunks, g, chunk_size, max_step_height);
    reopen_walkability_ramp_edges(area.chunks, g, chunk_size);
  }

  void rebuild_world_aggregates(WorldArea &area, int max_step_height) {
    rebuild_collision(area);
    rebuild_world_walkability(area, max_step_height);
    rebuild_world_portals(area);
  }

  float elevation_under(const WorldArea &area, float col_f, float row_f) noexcept {
    const int chunk_size = area.manifest.chunk_size;
    if (chunk_size <= 0)
      return 0.f;
    // std::floor (not truncate) so a fractionally-negative col_f/row_f identifies
    // the cell the player is actually in — the same convention as chunk_at_iso,
    // picking, and tilesmith. See std::floor sweep in the world-tilemap audit.
    const int col = static_cast<int>(std::floor(col_f));
    const int row = static_cast<int>(std::floor(row_f));
    return static_cast<float>(chunk_tile_elevation_at(area.chunks, col, row, chunk_size));
  }

  float elevation_under(const RenderState &state, float col_f, float row_f) noexcept {
    const auto *world_area = std::get_if<WorldArea>(&state.area);
    if (world_area != nullptr)
      return elevation_under(*world_area, col_f, row_f);
    const auto *single = std::get_if<SingleMapArea>(&state.area);
    if (single != nullptr && !single->map.tilemap.tilesets.empty())
      return world::tilemap::interpolated_elevation_at(single->map.tilemap, col_f, row_f);
    return 0.f;
  }

} // namespace corundum::render
