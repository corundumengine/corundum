#include <corundum/gameplay/world/tilemap/walkability.hpp>

#include <array>
#include <cmath>
#include <cstdlib>

namespace corundum::gameplay::world::tilemap {

  namespace {

    constexpr uint8_t k_all_dirs = DirN | DirNE | DirE | DirSE | DirS | DirSW | DirW | DirNW;

    // Maps a (dc,dr) delta in {-1,0,1}^2 to its WalkDir bit via a constexpr lookup
    // table. Returns 0 for any delta outside that set (not a single-step grid-adjacent move).
    [[nodiscard]] constexpr uint8_t dir_for_delta(int dc, int dr) noexcept {
      constexpr std::array<uint8_t, 9> k_lookup = {DirNW, DirN, DirNE, DirW, 0, DirE, DirSW, DirS, DirSE};
      if (dc < -1 || dc > 1 || dr < -1 || dr > 1)
        return 0;
      return k_lookup[static_cast<std::size_t>((dr + 1) * 3 + (dc + 1))];
    }

  } // namespace

  bool WalkabilityGraph::can_move(int from_col, int from_row, int to_col, int to_row) const noexcept {
    if (from_col < 0 || from_row < 0 || from_col >= width || from_row >= height)
      return true;
    if (to_col < 0 || to_row < 0 || to_col >= width || to_row >= height)
      return true;
    const int dc = to_col - from_col;
    const int dr = to_row - from_row;
    if (dc == 0 && dr == 0)
      return true;
    if (std::abs(dc) > 1 || std::abs(dr) > 1)
      return true;
    const uint8_t dir = dir_for_delta(dc, dr);
    const std::size_t idx =
        static_cast<std::size_t>(from_row) * static_cast<std::size_t>(width) + static_cast<std::size_t>(from_col);
    return (edges[idx] & dir) != 0;
  }

  void WalkabilityGraph::set_passable(int col_a, int row_a, int col_b, int row_b, bool passable) noexcept {
    if (col_a < 0 || row_a < 0 || col_a >= width || row_a >= height)
      return;
    if (col_b < 0 || row_b < 0 || col_b >= width || row_b >= height)
      return;
    const int dc = col_b - col_a;
    const int dr = row_b - row_a;
    if (std::abs(dc) > 1 || std::abs(dr) > 1 || (dc == 0 && dr == 0))
      return;
    const uint8_t dir_ab = dir_for_delta(dc, dr);
    const uint8_t dir_ba = dir_for_delta(-dc, -dr);
    const std::size_t idx_a =
        static_cast<std::size_t>(row_a) * static_cast<std::size_t>(width) + static_cast<std::size_t>(col_a);
    const std::size_t idx_b =
        static_cast<std::size_t>(row_b) * static_cast<std::size_t>(width) + static_cast<std::size_t>(col_b);
    if (passable) {
      edges[idx_a] |= dir_ab;
      edges[idx_b] |= dir_ba;
    } else {
      edges[idx_a] &= static_cast<uint8_t>(~dir_ab);
      edges[idx_b] &= static_cast<uint8_t>(~dir_ba);
    }
  }

  WalkabilityGraph build_walkability_graph(const Tilemap &tm, int max_step_height) noexcept {
    WalkabilityGraph g;
    g.width = tm.width;
    g.height = tm.height;
    g.edges.assign(static_cast<std::size_t>(tm.width) * static_cast<std::size_t>(tm.height), k_all_dirs);

    constexpr std::array<std::pair<int, int>, 8> k_neighbors{std::pair{0, -1}, std::pair{1, -1}, std::pair{1, 0},
                                                             std::pair{1, 1},  std::pair{0, 1},  std::pair{-1, 1},
                                                             std::pair{-1, 0}, std::pair{-1, -1}};

    for (int row = 0; row < tm.height; ++row) {
      for (int col = 0; col < tm.width; ++col) {
        const int elev = elevation_at(tm, col, row);
        const std::size_t idx =
            static_cast<std::size_t>(row) * static_cast<std::size_t>(tm.width) + static_cast<std::size_t>(col);
        for (const auto &[dc, dr] : k_neighbors) {
          const int ncol = col + dc;
          const int nrow = row + dr;
          if (ncol < 0 || nrow < 0 || ncol >= tm.width || nrow >= tm.height)
            continue;
          const int nelev = elevation_at(tm, ncol, nrow);
          if (std::abs(elev - nelev) > max_step_height)
            g.edges[idx] &= static_cast<uint8_t>(~dir_for_delta(dc, dr));
        }
      }
    }

    return g;
  }

} // namespace corundum::gameplay::world::tilemap
