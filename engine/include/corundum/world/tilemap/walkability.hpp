// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/world/tilemap/tilemap.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace corundum::world::tilemap {

  /**
   * @brief Bitmask for one of the 8 grid-adjacency directions from a cell.
   *
   * N/S/E/W here are grid axes (row decreasing/increasing, col increasing/
   * decreasing) — unrelated to TriangleCut's screen-space corner naming.
   * Isometric screen up/down/left/right map to grid *diagonals* (see
   * physics_system.cpp's apply_input), so these compass names describe the
   * (col,row) grid, not the rendered screen.
   */
  enum class WalkDir : uint8_t {
    North = 0x01,     ///< row - 1
    NorthEast = 0x02, ///< col + 1, row - 1
    East = 0x04,      ///< col + 1
    SouthEast = 0x08, ///< col + 1, row + 1
    South = 0x10,     ///< row + 1
    SouthWest = 0x20, ///< col - 1, row + 1
    West = 0x40,      ///< col - 1
    NorthWest = 0x80, ///< col - 1, row - 1
  };

  /**
   * @brief WalkDir bit for a single-step grid delta in {-1,0,1}^2, or 0 if the delta is not grid-adjacent.
   *
   * The single source of truth for the (col,row)-delta → WalkDir mapping; the world-mode graph builder in
   * render_system.cpp calls this rather than re-deriving the lookup table.
   */
  [[nodiscard]] constexpr uint8_t walk_dir_bit(int delta_col, int delta_row) noexcept {
    constexpr std::array<uint8_t, 9> k_lookup = {
        std::to_underlying(WalkDir::NorthWest),
        std::to_underlying(WalkDir::North),
        std::to_underlying(WalkDir::NorthEast),
        std::to_underlying(WalkDir::West),
        uint8_t{0},
        std::to_underlying(WalkDir::East),
        std::to_underlying(WalkDir::SouthWest),
        std::to_underlying(WalkDir::South),
        std::to_underlying(WalkDir::SouthEast),
    };
    if (delta_col < -1 || delta_col > 1 || delta_row < -1 || delta_row > 1)
      return 0;
    return k_lookup[(static_cast<std::size_t>(delta_row + 1) * 3) + static_cast<std::size_t>(delta_col + 1)];
  }

  /// Every WalkDir bit set — a freshly built cell starts fully connected.
  inline constexpr uint8_t k_all_walk_dirs =
      static_cast<uint8_t>(0u | std::to_underlying(WalkDir::North) | std::to_underlying(WalkDir::NorthEast) |
                           std::to_underlying(WalkDir::East) | std::to_underlying(WalkDir::SouthEast) |
                           std::to_underlying(WalkDir::South) | std::to_underlying(WalkDir::SouthWest) |
                           std::to_underlying(WalkDir::West) | std::to_underlying(WalkDir::NorthWest));

  /// The eight grid-adjacent (delta_col, delta_row) offsets, in WalkDir order.
  inline constexpr std::array<std::pair<int, int>, 8> k_walk_neighbors{
      std::pair{0, -1}, std::pair{1, -1}, std::pair{1, 0},  std::pair{1, 1},
      std::pair{0, 1},  std::pair{-1, 1}, std::pair{-1, 0}, std::pair{-1, -1},
  };

  /**
   * @brief Per-cell bitmask of which neighbor directions are traversable.
   *
   * Derived from elevation deltas (elevation_at()) and a max-step-height
   * allowance — separate from collision geometry (CollisionRect/
   * CollisionTriangle), which continues to gate movement independently.
   * Mutable at runtime via set_passable() so future systems (doors,
   * spawned obstacles) can open/close a specific edge without rebuilding
   * the whole graph.
   *
   * A graph normally spans a whole map with origin (0,0). World mode builds a graph
   * that spans only the active chunk window; col_origin/row_origin record where that
   * window's (0,0) cell sits in global world tile coordinates, and can_move() /
   * set_passable() subtract the origin from the (global) coordinates callers pass.
   */
  struct WalkabilityGraph {
    int width = 0;
    int height = 0;
    int col_origin = 0;         ///< Global tile col of cell (0,0). Non-zero only for the world-mode window graph.
    int row_origin = 0;         ///< Global tile row of cell (0,0). See col_origin.
    std::vector<uint8_t> edges; ///< width*height; bitmask of WalkDir passable FROM each cell.

    /**
     * @brief Can an entity step directly from (from_col,from_row) to (to_col,to_row)?
     *
     * @return true  if the cells are the same, or the step is grid-adjacent and its edge is open (or
     *               either cell is out of bounds — see @note).
     *         false if the step is not grid-adjacent (a multi-cell move, denied so a fast mover's
     *               substep loop cannot skip a gating decision), or the adjacent edge is closed.
     *
     * @note A false return does not distinguish a closed edge from a rejected multi-cell step;
     *       callers that must tell them apart have to check grid-adjacency themselves.
     * @note Out-of-bounds → true is intentional: it means no gating at map edges,
     *       so entities can walk across chunk/map boundaries and movement at the
     *       border isn't prematurely blocked. Callers wanting strict edge enforcement
     *       should check in_bounds() first.
     */
    [[nodiscard]] bool can_move(int from_col, int from_row, int to_col, int to_row) const noexcept;

    /**
     * @brief Set whether movement between two adjacent cells is allowed, symmetrically.
     *
     * No-op if the cells aren't grid-adjacent or either is out of bounds. The
     * primitive a future door system would call (open -> true, close -> false).
     */
    void set_passable(int col_a, int row_a, int col_b, int row_b, bool passable) noexcept;
  };

  /**
   * @brief Build a WalkabilityGraph for @p tm.
   *
   * Two passes. First: every cell starts fully connected (all 8 directions);
   * an edge is cleared whenever the two endpoints' elevation_at() differ by
   * more than @p max_step_height. Visiting every cell x direction pair
   * produces symmetric disconnection without extra bookkeeping. Second: any
   * cell with a ramp_axis_at() forces both directions along its axis back
   * open (N+S for RampAxis::NorthSouth, E+W for RampAxis::EastWest),
   * regardless of the elevation delta — the ramp's own two axis-neighbors
   * are the intended bridge endpoints. The other axis and all four
   * diagonals are untouched by this second pass.
   *
   * The returned graph has origin (0,0); the world-mode window graph is built
   * separately by render::rebuild_world_walkability().
   */
  [[nodiscard]] WalkabilityGraph build_walkability_graph(const Tilemap &tm, int max_step_height);

} // namespace corundum::world::tilemap
