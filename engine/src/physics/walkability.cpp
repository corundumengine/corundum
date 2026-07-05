#include <corundum/physics/walkability.hpp>

#include <cmath>

namespace corundum::physics::sys {

  void resolve_walkability(corundum::gameplay::component::Position &pos,
                           corundum::gameplay::component::Position prev_pos,
                           const corundum::gameplay::world::tilemap::WalkabilityGraph *graph) noexcept {
    if (!graph)
      return;

    const int prev_col = static_cast<int>(std::floor(prev_pos.col));
    const int prev_row = static_cast<int>(std::floor(prev_pos.row));

    // X axis: tentative col with prev row.
    const int to_col = static_cast<int>(std::floor(pos.col));
    if (to_col != prev_col && !graph->can_move(prev_col, prev_row, to_col, prev_row))
      pos.col = prev_pos.col;

    // Y axis: resolved col with tentative row.
    const int cur_col = static_cast<int>(std::floor(pos.col));
    const int to_row = static_cast<int>(std::floor(pos.row));
    if (to_row != prev_row && !graph->can_move(cur_col, prev_row, cur_col, to_row))
      pos.row = prev_pos.row;
  }

} // namespace corundum::physics::sys
