#include <corundum/gameplay/sys/pathfinding.hpp>
#include <corundum/gameplay/world/tilemap/tilemap.hpp>
#include <corundum/gameplay/world/update.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <queue>
#include <utility>

namespace corundum::gameplay::sys {

  namespace {

    // Mirrors physics_sys.cpp's elevation-aware collision gating: a shape only blocks
    // an entity (here, a path node) whose own elevation is within this many units.
    constexpr int k_elevation_tolerance = 0;
    constexpr float k_sqrt2 = 1.41421356f;

    [[nodiscard]] bool cell_blocked_by_collision(int col, int row,
                                                 const corundum::gameplay::world::tilemap::Tilemap &tm,
                                                 const corundum::gameplay::world::MapView &map) noexcept {
      using corundum::gameplay::world::tilemap::elevation_at;
      const int cell_elev = elevation_at(tm, col, row);
      const float c0 = static_cast<float>(col), c1 = c0 + 1.f;
      const float r0 = static_cast<float>(row), r1 = r0 + 1.f;

      const corundum::gameplay::world::tilemap::CollisionRectsView &rects = map.collisions;
      for (std::size_t i = 0; i < rects.size(); ++i) {
        if (!rects.elevations.empty() &&
            std::abs(static_cast<int>(rects.elevations[i]) - cell_elev) > k_elevation_tolerance)
          continue;
        if (c0 < rects.cols[i] + rects.col_spans[i] && c1 > rects.cols[i] && r0 < rects.rows[i] + rects.row_spans[i] &&
            r1 > rects.rows[i])
          return true;
      }

      const corundum::gameplay::world::tilemap::CollisionTrianglesView &tris = map.collision_triangles;
      for (std::size_t i = 0; i < tris.size(); ++i) {
        if (!tris.elevations.empty() &&
            std::abs(static_cast<int>(tris.elevations[i]) - cell_elev) > k_elevation_tolerance)
          continue;
        // Conservative: bounding-box overlap, not the exact diagonal half. Worst case a
        // path avoids a technically-open corner rather than routing through a wall.
        if (c0 < tris.cols[i] + tris.col_spans[i] && c1 > tris.cols[i] && r0 < tris.rows[i] + tris.row_spans[i] &&
            r1 > tris.rows[i])
          return true;
      }

      return false;
    }

    [[nodiscard]] float octile_heuristic(TileCoord a, TileCoord b) noexcept {
      constexpr float k_sqrt2_minus_1 = 0.41421356f;
      const float dx = std::abs(static_cast<float>(a.col - b.col));
      const float dy = std::abs(static_cast<float>(a.row - b.row));
      return std::max(dx, dy) + k_sqrt2_minus_1 * std::min(dx, dy);
    }

    struct OpenEntry {
      float f;
      int idx;

      // Min-heap via std::priority_queue (a max-heap by default), so invert the comparison.
      [[nodiscard]] friend bool operator<(const OpenEntry &a, const OpenEntry &b) noexcept {
        return a.f > b.f;
      }
    };

  } // namespace

  std::vector<TileCoord> find_path(const corundum::gameplay::world::MapView &map, TileCoord start,
                                   TileCoord goal) noexcept {
    if (!map.walkability || !map.elevation_map)
      return {};
    const corundum::gameplay::world::tilemap::WalkabilityGraph &graph = *map.walkability;
    const corundum::gameplay::world::tilemap::Tilemap &tm = *map.elevation_map;
    const int width = graph.width;
    const int height = graph.height;

    if (start.col < 0 || start.row < 0 || start.col >= width || start.row >= height)
      return {};
    if (goal.col < 0 || goal.row < 0 || goal.col >= width || goal.row >= height)
      return {};
    if (start == goal)
      return {};

    const auto index_of = [width](TileCoord c) noexcept { return c.row * width + c.col; };

    const std::size_t n = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    std::vector<float> g_score(n, std::numeric_limits<float>::infinity());
    std::vector<int> came_from(n, -1);
    std::vector<bool> closed(n, false);

    std::priority_queue<OpenEntry> open;
    g_score[static_cast<std::size_t>(index_of(start))] = 0.f;
    open.push({octile_heuristic(start, goal), index_of(start)});

    constexpr std::array<std::pair<int, int>, 8> k_neighbors{
        {{0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}}};

    while (!open.empty()) {
      const int cur_idx = open.top().idx;
      open.pop();
      if (closed[static_cast<std::size_t>(cur_idx)])
        continue;
      closed[static_cast<std::size_t>(cur_idx)] = true;

      const TileCoord cur{cur_idx % width, cur_idx / width};
      if (cur == goal) {
        std::vector<TileCoord> path;
        int idx = cur_idx;
        while (idx != index_of(start)) {
          path.push_back({idx % width, idx / width});
          idx = came_from[static_cast<std::size_t>(idx)];
        }
        std::ranges::reverse(path);
        return path;
      }

      for (const auto &[dc, dr] : k_neighbors) {
        const TileCoord next{cur.col + dc, cur.row + dr};
        if (next.col < 0 || next.row < 0 || next.col >= width || next.row >= height)
          continue;
        if (!graph.can_move(cur.col, cur.row, next.col, next.row))
          continue;
        // Corner-cut prevention: a diagonal step needs both flanking cardinal steps open.
        if (dc != 0 && dr != 0) {
          if (!graph.can_move(cur.col, cur.row, cur.col + dc, cur.row) ||
              !graph.can_move(cur.col, cur.row, cur.col, cur.row + dr))
            continue;
        }
        if (cell_blocked_by_collision(next.col, next.row, tm, map))
          continue;

        const float step_cost = (dc != 0 && dr != 0) ? k_sqrt2 : 1.f;
        const float tentative_g = g_score[static_cast<std::size_t>(cur_idx)] + step_cost;
        const int next_idx = index_of(next);
        if (tentative_g < g_score[static_cast<std::size_t>(next_idx)]) {
          g_score[static_cast<std::size_t>(next_idx)] = tentative_g;
          came_from[static_cast<std::size_t>(next_idx)] = cur_idx;
          open.push({tentative_g + octile_heuristic(next, goal), next_idx});
        }
      }
    }

    return {}; // unreachable
  }

} // namespace corundum::gameplay::sys
