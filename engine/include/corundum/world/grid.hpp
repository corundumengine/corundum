// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/entities/tables/collision_table.hpp>
#include <corundum/world/picking.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>

// Tile-grid queries over entity positions. A whole position is the tile an entity stands on, and
// k_entity_anchor_offset puts its feet at that tile's centre, so mid-step the tile it occupies is
// the one under its feet: its position rounded, not floored.
namespace corundum::world {

  /** @brief The tile under the feet of an entity at (@p col, @p row). */
  [[nodiscard]] inline TileCoord tile_of(float col, float row) noexcept {
    return {
        .col = static_cast<int>(std::floor(col + entities::k_entity_anchor_offset)),
        .row = static_cast<int>(std::floor(row + entities::k_entity_anchor_offset)),
    };
  }

  /** @brief Steps between two tiles when a diagonal step counts as one (Chebyshev distance). */
  [[nodiscard]] inline int tile_distance(TileCoord a, TileCoord b) noexcept {
    return std::max(std::abs(a.col - b.col), std::abs(a.row - b.row));
  }

} // namespace corundum::world
