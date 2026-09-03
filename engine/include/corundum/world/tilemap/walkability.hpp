#pragma once
#include <corundum/world/tilemap/tilemap.hpp>

#include <cstdint>
#include <vector>

namespace corundum::world::tilemap {

  /**
   * @brief Bitmask for one of the 8 grid-adjacency directions from a cell.
   *
   * N/S/E/W here are grid axes (row decreasing/increasing, col increasing/
   * decreasing) — unrelated to TriangleCut's screen-space corner naming.
   * Isometric screen up/down/left/right map to grid *diagonals* (see
   * physics_sys.cpp's apply_input), so these compass names describe the
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
     * Returns true (no gating) if either cell is out of bounds, the cells are the
     * same, or the cells aren't grid-adjacent (delta > 1 on either axis) — mirroring
     * how the existing collision system also doesn't handle multi-cell-per-frame
     * tunneling; not a new limitation introduced here.
     *
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
  [[nodiscard]] WalkabilityGraph build_walkability_graph(const Tilemap &tm, int max_step_height) noexcept;

} // namespace corundum::world::tilemap
