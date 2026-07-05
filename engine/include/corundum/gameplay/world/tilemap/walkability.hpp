#pragma once
#include <corundum/gameplay/world/tilemap/tilemap.hpp>

#include <cstdint>
#include <vector>

namespace corundum::gameplay::world::tilemap {

  /**
   * @brief Bitmask for one of the 8 grid-adjacency directions from a cell.
   *
   * N/S/E/W here are grid axes (row decreasing/increasing, col increasing/
   * decreasing) — unrelated to TriangleCut's screen-space corner naming.
   * Isometric screen up/down/left/right map to grid *diagonals* (see
   * physics_sys.cpp's apply_input), so these compass names describe the
   * (col,row) grid, not the rendered screen.
   */
  enum WalkDir : uint8_t {
    DirN = 0x01,  ///< row - 1
    DirNE = 0x02, ///< col + 1, row - 1
    DirE = 0x04,  ///< col + 1
    DirSE = 0x08, ///< col + 1, row + 1
    DirS = 0x10,  ///< row + 1
    DirSW = 0x20, ///< col - 1, row + 1
    DirW = 0x40,  ///< col - 1
    DirNW = 0x80, ///< col - 1, row - 1
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
   */
  struct WalkabilityGraph {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> edges; ///< width*height; bitmask of WalkDir passable FROM each cell.

    /**
     * @brief Can an entity step directly from (from_col,from_row) to (to_col,to_row)?
     *
     * Returns true (no gating) if either cell is out of bounds, the cells are the
     * same, or the cells aren't grid-adjacent (delta > 1 on either axis) — mirroring
     * how the existing collision system also doesn't handle multi-cell-per-frame
     * tunneling; not a new limitation introduced here.
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
   * Every cell starts fully connected (all 8 directions); an edge is cleared
   * whenever the two endpoints' elevation_at() differ by more than
   * @p max_step_height. Visiting every cell x direction pair produces
   * symmetric disconnection without extra bookkeeping.
   */
  [[nodiscard]] WalkabilityGraph build_walkability_graph(const Tilemap &tm, int max_step_height) noexcept;

} // namespace corundum::gameplay::world::tilemap
