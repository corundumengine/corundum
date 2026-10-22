// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/render/render_state.hpp>

/// World-mode data derived from the active chunk window (collision, walkability, portals) and
/// the global-tile queries that resolve a tile to its owning chunk.
namespace corundum::render {

  /** @brief Elevation of the tile under (col_f, row_f), resolving chunk ownership in world mode.
   *
   * In world mode looks up the owning chunk via the active-chunk window and returns
   * discrete elevation. In single-map mode delegates to interpolated_elevation_at().
   * Returns 0 if no tilemap is loaded at the given coordinates.
   *
   * @param[in] state Render state carrying active-chunk data or single-tilemap data.
   * @param[in] col_f Fractional tile column.
   * @param[in] row_f Fractional tile row.
   * @return Tile elevation (≥0) at the queried position; 0 when out of bounds.
   */
  [[nodiscard]] float elevation_under(const render::RenderState &state, float col_f, float row_f) noexcept;

  /** @brief Rebuild the world-mode aggregated collision rects and triangles from active chunks.
   *
   * Clears and repopulates @c state.agg_collisions and @c state.agg_triangles by
   * walking @c state.chunks.active() and offsetting each chunk's tile-grid-local
   * collision geometry into world tile-grid coordinates (offsets are in tile units,
   * matching the tile-grid coordinate space the rest of the resolver operates in).
   *
   * @param[in,out] state  Render state in World mode whose aggregates will be overwritten.
   * @note No-op when the chunk window has no active chunks.
   */
  void rebuild_collision(render::RenderState &state);

  /** @brief Rebuild the world-mode aggregated walkability graph from the active chunks.
   *
   * Builds a WalkabilityGraph spanning the bounding box of the active chunk window,
   * indexed in global world tile coordinates (the graph's col_origin/row_origin record
   * the offset). Elevation-delta gating uses global lookups, so chunk seams gate
   * correctly. Clears the graph (no-op otherwise) when the chunk window is empty.
   *
   * @param[in,out] state           Render state in World mode.
   * @param[in]     max_step_height  Max walkable elevation delta (GameConfig::max_step_height).
   */
  void rebuild_world_walkability(render::RenderState &state, int max_step_height);

  /** @brief Rebuild every world-mode aggregate for the active chunk window.
   *
   * Runs rebuild_collision(), rebuild_world_walkability() and the portal aggregation in one
   * step so the three stay in sync with the active chunk set. stream_world_chunks() and
   * load_world() call it after chunk mutations.
   *
   * @param[in,out] state           Render state in World mode.
   * @param[in]     max_step_height  Max walkable elevation delta (GameConfig::max_step_height).
   */
  void rebuild_world_aggregates(render::RenderState &state, int max_step_height);

} // namespace corundum::render
