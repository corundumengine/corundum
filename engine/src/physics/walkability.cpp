// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/entities/components.hpp>
#include <corundum/physics/walkability.hpp>
#include <corundum/world/tilemap/walkability.hpp>

#include <cmath>

namespace corundum::physics {

  namespace {

    /// Step one axis back to @p axis_prev when @p graph disconnects the crossing, and leave
    /// it untouched otherwise — the axis-separated slide resolve_collisions uses.
    [[nodiscard]] float resolve_axis(float axis_pos, float axis_prev, int from_col, int from_row, int to_col,
                                     int to_row, const corundum::world::tilemap::WalkabilityGraph &graph) noexcept {
      if (from_col == to_col && from_row == to_row)
        return axis_pos;
      return graph.can_move(from_col, from_row, to_col, to_row) ? axis_pos : axis_prev;
    }

  } // namespace

  void resolve_walkability(corundum::entities::Position &pos, corundum::entities::Position prev_pos,
                           const corundum::world::tilemap::WalkabilityGraph *graph) noexcept {
    if (graph == nullptr)
      return;

    const int prev_col = static_cast<int>(std::floor(prev_pos.col));
    const int prev_row = static_cast<int>(std::floor(prev_pos.row));

    const int to_col = static_cast<int>(std::floor(pos.col));
    pos.col = resolve_axis(pos.col, prev_pos.col, prev_col, prev_row, to_col, prev_row, *graph);

    // Row resolves last, against the cell the column pass settled on. A step crossing both
    // axes is therefore a diagonal from the frame-start cell, so it is gated by the graph's
    // diagonal edge, not the two cardinal edges — both cardinals can be open across a corner
    // whose own elevation step still exceeds max_step_height.
    const int resolved_col = static_cast<int>(std::floor(pos.col));
    const int to_row = static_cast<int>(std::floor(pos.row));
    pos.row = resolve_axis(pos.row, prev_pos.row, prev_col, prev_row, resolved_col, to_row, *graph);
  }

} // namespace corundum::physics
