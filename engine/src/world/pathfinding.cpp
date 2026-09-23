// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/entities/entity.hpp>
#include <corundum/entities/tables/collision_table.hpp>
#include <corundum/entities/tables/transform_table.hpp>
#include <corundum/physics/physics_system.hpp>
#include <corundum/world/map_view.hpp>
#include <corundum/world/pathfinding.hpp>
#include <corundum/world/picking.hpp>
#include <corundum/world/tilemap/tilemap.hpp>
#include <corundum/world/tilemap/walkability.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <span>
#include <vector>

namespace corundum::world {

  namespace {

    constexpr float k_sqrt2 = std::numbers::sqrt2_v<float>;
    constexpr float k_sqrt2_minus_1 = k_sqrt2 - 1.f;

    /** @brief True if @p cell lies inside @p graph's window (global tile coordinates). */
    [[nodiscard]] bool in_graph_bounds(TileCoord cell,
                                       const corundum::world::tilemap::WalkabilityGraph &graph) noexcept {
      return cell.col >= graph.col_origin && cell.row >= graph.row_origin &&
             cell.col < graph.col_origin + graph.width && cell.row < graph.row_origin + graph.height;
    }

    /** @brief Row-major index of @p cell into @p graph's window. @pre cell is in bounds and width > 0. */
    [[nodiscard]] std::size_t index_of(TileCoord cell, int width, int col0, int row0) noexcept {
      return (static_cast<std::size_t>(cell.row - row0) * static_cast<std::size_t>(width)) +
             static_cast<std::size_t>(cell.col - col0);
    }

    /** @brief Global tile coordinate for a row-major window index. @pre width > 0. */
    [[nodiscard]] TileCoord from_index(std::size_t idx, int width, int col0, int row0) noexcept {
      const auto cell_width = static_cast<std::size_t>(width);
      return {.col = static_cast<int>(idx % cell_width) + col0, .row = static_cast<int>(idx / cell_width) + row0};
    }

    /** @brief True if the unit cell [c0,c1) x [r0,r1) overlaps the AABB (@p col, @p row, spans). */
    [[nodiscard]] bool cell_overlaps(float c0, float c1, float r0, float r1, float col, float row, float col_span,
                                     float row_span) noexcept {
      return c0 < col + col_span && c1 > col && r0 < row + row_span && r1 > row;
    }

    /** @brief True if the unit cell [c0,c1) x [r0,r1) overlaps @p box. */
    [[nodiscard]] bool cell_overlaps(float c0, float c1, float r0, float r1,
                                     const corundum::entities::GridBox &box) noexcept {
      return cell_overlaps(c0, c1, r0, r1, box.col, box.row, box.col_span, box.row_span);
    }

    /** @brief True if collider @p i's elevation is outside @p gate's tolerance of the cell's own elevation. */
    [[nodiscard]] bool elevation_outside_gate(std::span<const uint8_t> elevations, std::size_t i,
                                              const corundum::physics::ElevationGate &gate) noexcept {
      return !elevations.empty() && std::abs(static_cast<int>(elevations[i]) - gate.player_elevation) > gate.tolerance;
    }

    [[nodiscard]] bool cell_blocked_by_collision(int col, int row, const corundum::world::MapView &map,
                                                 const corundum::entities::CollisionTable *npc_collisions,
                                                 const corundum::entities::TransformTable *npc_transforms,
                                                 corundum::entities::EntityId exclude) noexcept {
      // Gate authored colliders against the candidate cell's own elevation, using the same
      // ramp-aware gate physics applies to the mover (see compute_elevation_gate).
      const corundum::physics::ElevationGate gate = corundum::physics::compute_elevation_gate(
          map, static_cast<float>(col) + 0.5f, static_cast<float>(row) + 0.5f);
      const float c0 = static_cast<float>(col);
      const float c1 = c0 + 1.f;
      const float r0 = static_cast<float>(row);
      const float r1 = r0 + 1.f;

      const corundum::world::tilemap::CollisionRectsView &rects = map.collisions;
      for (std::size_t i = 0; i < rects.size(); ++i) {
        if (elevation_outside_gate(rects.elevations, i, gate))
          continue;
        if (cell_overlaps(c0, c1, r0, r1, rects.cols[i], rects.rows[i], rects.col_spans[i], rects.row_spans[i]))
          return true;
      }

      // Diagonal half-tile colliders are tested by their AABB: conservative (a cell whose
      // open half the path could use is still rejected), but cheap and rarely wrong enough
      // to matter for routing.
      const corundum::world::tilemap::CollisionTrianglesView &tris = map.collision_triangles;
      for (std::size_t i = 0; i < tris.size(); ++i) {
        if (elevation_outside_gate(tris.elevations, i, gate))
          continue;
        if (cell_overlaps(c0, c1, r0, r1, tris.cols[i], tris.rows[i], tris.col_spans[i], tris.row_spans[i]))
          return true;
      }

      if (npc_collisions == nullptr || npc_transforms == nullptr)
        return false;

      const auto npc_rects = npc_collisions->active_rects();
      const auto npc_entities = npc_collisions->active_entities();
      for (std::size_t i = 0; i < npc_rects.size(); ++i) {
        const corundum::entities::EntityId entity = npc_entities[i];
        if (entity == exclude) // the mover's own footprint straddles the cells around its feet
          continue;
        if (!npc_transforms->has(entity)) // collision and transform tables can drift apart
          continue;
        const std::uint32_t slot = npc_transforms->dense_index(entity);
        const float npc_col = npc_transforms->col[slot];
        const float npc_row = npc_transforms->row[slot];
        const int npc_elev = corundum::world::discrete_elevation_at(map, static_cast<int>(std::floor(npc_col)),
                                                                    static_cast<int>(std::floor(npc_row)));
        if (std::abs(npc_elev - gate.player_elevation) > gate.tolerance)
          continue;
        const corundum::entities::GridBox npc_box =
            corundum::entities::footprint_of(npc_col, npc_row, npc_rects[i].col_span, npc_rects[i].row_span);
        if (cell_overlaps(c0, c1, r0, r1, npc_box))
          return true;
      }

      return false;
    }

    /** @brief Per-cell collision test with a lazy memo, so A* rescanning a cell stays O(1). */
    struct CollisionQuery {
      const corundum::world::MapView *map;
      const corundum::entities::CollisionTable *npc_collisions;
      const corundum::entities::TransformTable *npc_transforms;
      corundum::entities::EntityId exclude;
      int width;
      int col0;
      int row0;
      std::vector<std::int8_t> memo; ///< -1 unknown, 0 clear, 1 blocked; lazily filled.

      [[nodiscard]] bool blocked(TileCoord cell) noexcept {
        std::int8_t &cached = memo[index_of(cell, width, col0, row0)];
        if (cached < 0)
          cached = cell_blocked_by_collision(cell.col, cell.row, *map, npc_collisions, npc_transforms, exclude) ? 1 : 0;
        return cached != 0;
      }
    };

    /** @brief True if a step of (@p dc, @p dr) from @p cur clears the graph edges, corner-cut rule, and collision. */
    [[nodiscard]] bool step_allowed(const corundum::world::tilemap::WalkabilityGraph &graph, CollisionQuery &collision,
                                    TileCoord cur, int dc, int dr) noexcept {
      const TileCoord next{.col = cur.col + dc, .row = cur.row + dr};
      if (!in_graph_bounds(next, graph))
        return false;
      if (!graph.can_move(cur.col, cur.row, next.col, next.row))
        return false;
      // Corner-cut prevention: a diagonal step needs both flanking cardinal steps open,
      // and neither flanking cell may itself be blocked by collision geometry.
      if (dc != 0 && dr != 0) {
        if (!graph.can_move(cur.col, cur.row, cur.col + dc, cur.row) ||
            !graph.can_move(cur.col, cur.row, cur.col, cur.row + dr))
          return false;
        const TileCoord flank_col{.col = cur.col + dc, .row = cur.row};
        const TileCoord flank_row{.col = cur.col, .row = cur.row + dr};
        if ((in_graph_bounds(flank_col, graph) && collision.blocked(flank_col)) ||
            (in_graph_bounds(flank_row, graph) && collision.blocked(flank_row)))
          return false;
      }
      return !collision.blocked(next);
    }

    /** @brief Walk @p came_from back from @p goal_idx, excluding the start cell. */
    [[nodiscard]] std::vector<TileCoord> reconstruct_path(std::size_t goal_idx, std::size_t start_idx,
                                                          const std::vector<std::size_t> &came_from, int width,
                                                          int col0, int row0) {
      std::vector<TileCoord> path;
      for (std::size_t idx = goal_idx; idx != start_idx; idx = came_from[idx])
        path.push_back(from_index(idx, width, col0, row0));
      std::ranges::reverse(path);
      return path;
    }

    [[nodiscard]] float octile_heuristic(TileCoord a, TileCoord b) noexcept {
      const float dx = std::abs(static_cast<float>(a.col - b.col));
      const float dy = std::abs(static_cast<float>(a.row - b.row));
      return std::max(dx, dy) + (k_sqrt2_minus_1 * std::min(dx, dy));
    }

    struct OpenEntry {
      float f;
      std::size_t idx; ///< Index into the search grid.
    };

    // Min-heap comparator for std::ranges heap algorithms: the lowest f sorts first.
    [[nodiscard]] bool open_entry_before(const OpenEntry &a, const OpenEntry &b) noexcept {
      return a.f > b.f;
    }

  } // namespace

  std::vector<TileCoord> find_path(const corundum::world::MapView &map, TileCoord start, TileCoord goal,
                                   const corundum::entities::CollisionTable *npc_collisions,
                                   const corundum::entities::TransformTable *npc_transforms,
                                   corundum::entities::EntityId exclude) {
    if (map.walkability == nullptr)
      return {};
    const corundum::world::tilemap::WalkabilityGraph &graph = *map.walkability;
    if (graph.width <= 0 || graph.height <= 0)
      return {};
    const int width = graph.width;
    const int height = graph.height;
    if (!in_graph_bounds(start, graph) || !in_graph_bounds(goal, graph))
      return {};
    if (start == goal)
      return {};

    const std::size_t n = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    std::vector<float> g_score(n, std::numeric_limits<float>::infinity());
    std::vector<std::size_t> came_from(n, n); // n is not a valid index, so it doubles as "no predecessor"
    std::vector<bool> closed(n, false);
    CollisionQuery collision{
        .map = &map,
        .npc_collisions = npc_collisions,
        .npc_transforms = npc_transforms,
        .exclude = exclude,
        .width = width,
        .col0 = graph.col_origin,
        .row0 = graph.row_origin,
        .memo = std::vector<std::int8_t>(n, -1),
    };

    const std::size_t start_idx = index_of(start, width, graph.col_origin, graph.row_origin);
    std::vector<OpenEntry> open;
    open.reserve((n / 4) + 1);
    g_score[start_idx] = 0.f;
    open.push_back({.f = octile_heuristic(start, goal), .idx = start_idx});
    std::ranges::push_heap(open, open_entry_before);

    while (!open.empty()) {
      std::ranges::pop_heap(open, open_entry_before);
      const std::size_t cur_idx = open.back().idx;
      open.pop_back();
      if (closed[cur_idx])
        continue;
      closed[cur_idx] = true;

      const TileCoord cur = from_index(cur_idx, width, graph.col_origin, graph.row_origin);
      if (cur == goal)
        return reconstruct_path(cur_idx, start_idx, came_from, width, graph.col_origin, graph.row_origin);

      for (const auto &[dc, dr] : corundum::world::tilemap::k_walk_neighbors) {
        if (!step_allowed(graph, collision, cur, dc, dr))
          continue;
        const TileCoord next{.col = cur.col + dc, .row = cur.row + dr};
        const float step_cost = (dc != 0 && dr != 0) ? k_sqrt2 : 1.f;
        const float tentative_g = g_score[cur_idx] + step_cost;
        const std::size_t next_idx = index_of(next, width, graph.col_origin, graph.row_origin);
        if (tentative_g < g_score[next_idx]) {
          g_score[next_idx] = tentative_g;
          came_from[next_idx] = cur_idx;
          open.push_back({.f = tentative_g + octile_heuristic(next, goal), .idx = next_idx});
          std::ranges::push_heap(open, open_entry_before);
        }
      }
    }

    return {}; // unreachable
  }

} // namespace corundum::world
