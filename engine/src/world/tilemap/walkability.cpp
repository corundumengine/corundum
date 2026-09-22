// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/world/tilemap/tilemap.hpp>
#include <corundum/world/tilemap/walkability.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <utility>
#include <vector>

namespace corundum::world::tilemap {

  namespace {

    /// Linear index of cell (col, row) in a width-wide row-major grid.
    [[nodiscard]] constexpr std::size_t cell_index(int col, int row, int width) noexcept {
      return (static_cast<std::size_t>(row) * static_cast<std::size_t>(width)) + static_cast<std::size_t>(col);
    }

    /// Elevation of every cell, cached row-major: the neighbour scan samples each cell up to nine
    /// times, and elevation_at() re-scans every z-index-0 layer on each call.
    [[nodiscard]] std::vector<int> cache_cell_elevations(const Tilemap &tm, std::size_t cell_count) {
      std::vector<int> elevations(cell_count);
      for (int row = 0; row < tm.height; ++row) {
        for (int col = 0; col < tm.width; ++col) {
          elevations[cell_index(col, row, tm.width)] = elevation_at(tm, col, row);
        }
      }
      return elevations;
    }

    /// First pass: clear the edge from each cell to any neighbour whose elevation differs by more
    /// than @p max_step_height.
    void clear_steep_edges(const Tilemap &tm, WalkabilityGraph &g, const std::vector<int> &elevations,
                           int max_step_height) {
      for (int row = 0; row < tm.height; ++row) {
        for (int col = 0; col < tm.width; ++col) {
          const std::size_t idx = cell_index(col, row, tm.width);
          const int elevation = elevations[idx];
          for (const auto &[delta_col, delta_row] : k_walk_neighbors) {
            const int neighbor_col = col + delta_col;
            const int neighbor_row = row + delta_row;
            if (neighbor_col < 0 || neighbor_row < 0 || neighbor_col >= tm.width || neighbor_row >= tm.height)
              continue;
            if (std::abs(elevation - elevations[cell_index(neighbor_col, neighbor_row, tm.width)]) > max_step_height)
              g.edges[idx] &= static_cast<uint8_t>(~walk_dir_bit(delta_col, delta_row));
          }
        }
      }
    }

    /// Second pass: a ramp cell forces both directions along its axis back open, overriding the
    /// max_step_height disconnect above for exactly those two directions — the ramp's height delta
    /// is inferred from its two axis-neighbors' elevations, never stored directly. Diagonals and
    /// the other axis are untouched and still governed normally.
    void reopen_ramp_edges(const Tilemap &tm, WalkabilityGraph &g) {
      for (int row = 0; row < tm.height; ++row) {
        for (int col = 0; col < tm.width; ++col) {
          const std::optional<RampAxis> axis = ramp_axis_at(tm, col, row);
          if (!axis)
            continue;
          const auto [delta_col_axis, delta_row_axis] =
              *axis == RampAxis::NorthSouth ? std::pair{0, -1} : std::pair{1, 0};
          g.set_passable(col, row, col + delta_col_axis, row + delta_row_axis, true);
          g.set_passable(col, row, col - delta_col_axis, row - delta_row_axis, true);
        }
      }
    }

  } // namespace

  bool WalkabilityGraph::can_move(int from_col, int from_row, int to_col, int to_row) const noexcept {
    // Callers pass global tile coords; window graphs offset their cells by (col_origin,
    // row_origin). Single-map graphs have origin (0,0), so this is a no-op there.
    from_col -= col_origin;
    from_row -= row_origin;
    to_col -= col_origin;
    to_row -= row_origin;
    if (from_col < 0 || from_row < 0 || from_col >= width || from_row >= height)
      return true;
    if (to_col < 0 || to_row < 0 || to_col >= width || to_row >= height)
      return true;
    const int delta_col = to_col - from_col;
    const int delta_row = to_row - from_row;
    if (delta_col == 0 && delta_row == 0)
      return true;

    // Multi-cell moves are denied rather than bypassed, so a fast mover's per-cell substep loop
    // (physics_system.cpp) can never skip an edge decision. The out-of-bounds returns above stay
    // unblocked: cells outside the map have no graph entry.
    if (std::abs(delta_col) > 1 || std::abs(delta_row) > 1)
      return false;
    const std::size_t idx = cell_index(from_col, from_row, width);
    return (edges[idx] & walk_dir_bit(delta_col, delta_row)) != 0;
  }

  void WalkabilityGraph::set_passable(int col_a, int row_a, int col_b, int row_b, bool passable) noexcept {
    col_a -= col_origin;
    row_a -= row_origin;
    col_b -= col_origin;
    row_b -= row_origin;
    if (col_a < 0 || row_a < 0 || col_a >= width || row_a >= height)
      return;
    if (col_b < 0 || row_b < 0 || col_b >= width || row_b >= height)
      return;
    const int delta_col = col_b - col_a;
    const int delta_row = row_b - row_a;
    if (std::abs(delta_col) > 1 || std::abs(delta_row) > 1 || (delta_col == 0 && delta_row == 0))
      return;
    const uint8_t dir_ab = walk_dir_bit(delta_col, delta_row);
    const uint8_t dir_ba = walk_dir_bit(-delta_col, -delta_row);
    const std::size_t idx_a = cell_index(col_a, row_a, width);
    const std::size_t idx_b = cell_index(col_b, row_b, width);
    if (passable) {
      edges[idx_a] |= dir_ab;
      edges[idx_b] |= dir_ba;
    } else {
      edges[idx_a] &= static_cast<uint8_t>(~dir_ab);
      edges[idx_b] &= static_cast<uint8_t>(~dir_ba);
    }
  }

  WalkabilityGraph build_walkability_graph(const Tilemap &tm, int max_step_height) {
    const std::size_t cell_count = static_cast<std::size_t>(tm.width) * static_cast<std::size_t>(tm.height);

    WalkabilityGraph g;
    g.width = tm.width;
    g.height = tm.height;
    g.edges.assign(cell_count, k_all_walk_dirs);

    const std::vector<int> elevations = cache_cell_elevations(tm, cell_count);
    clear_steep_edges(tm, g, elevations, max_step_height);
    reopen_ramp_edges(tm, g);
    return g;
  }

} // namespace corundum::world::tilemap
