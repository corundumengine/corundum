// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/render/render_state.hpp>

/// World-mode data derived from the active chunk window (collision, walkability, portals) and
/// the global-tile queries that resolve a tile to its owning chunk.
namespace corundum::render {

  /** @brief Elevation of the world tile under (col_f, row_f), resolved through the active chunk window.
   *  Discrete per tile; 0 outside the window or when no chunk is resident. */
  [[nodiscard]] float elevation_under(const WorldArea &area, float col_f, float row_f) noexcept;

  /** @brief Elevation under (col_f, row_f) in the loaded area: discrete via the chunk window in World
   *  mode, interpolated in single-map mode, 0 with no area or outside it. */
  [[nodiscard]] float elevation_under(const RenderState &state, float col_f, float row_f) noexcept;

  /** @brief Rebuild the world-mode aggregated collision rects and triangles from active chunks.
   *
   * Clears and repopulates @c area.aggregates.collisions and @c area.aggregates.triangles by
   * walking @c area.chunks.active() and offsetting each chunk's tile-grid-local
   * collision geometry into world tile-grid coordinates (offsets are in tile units,
   * matching the tile-grid coordinate space the rest of the resolver operates in).
   *
   * @param[in,out] area  World area whose aggregates will be overwritten.
   * @note No-op when the chunk window has no active chunks.
   */
  void rebuild_collision(WorldArea &area);

  /** @brief Rebuild the world-mode aggregated walkability graph from the active chunks.
   *
   * Builds a WalkabilityGraph spanning the bounding box of the active chunk window,
   * indexed in global world tile coordinates (the graph's col_origin/row_origin record
   * the offset). Elevation-delta gating uses global lookups, so chunk seams gate
   * correctly. Clears the graph (no-op otherwise) when the chunk window is empty.
   *
   * @param[in,out] area            World area.
   * @param[in]     max_step_height  Max walkable elevation delta (GameConfig::max_step_height).
   */
  void rebuild_world_walkability(WorldArea &area, int max_step_height);

  /** @brief Rebuild every world-mode aggregate for the active chunk window.
   *
   * Runs rebuild_collision(), rebuild_world_walkability() and the portal aggregation in one
   * step so the three stay in sync with the active chunk set. stream_world_chunks() and
   * load_world() call it after chunk mutations.
   *
   * @param[in,out] area            World area.
   * @param[in]     max_step_height  Max walkable elevation delta (GameConfig::max_step_height).
   */
  void rebuild_world_aggregates(WorldArea &area, int max_step_height);

} // namespace corundum::render
