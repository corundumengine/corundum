// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

namespace corundum::core {

  /// Cardinal and intercardinal screen-space directions. Count is a sentinel; keep it last.
  enum class Direction : uint8_t { South, North, East, West, NorthEast, SouthEast, SouthWest, NorthWest, Count };

  /// Number of real directions; use as array extent.
  inline constexpr uint8_t k_num_directions = static_cast<uint8_t>(Direction::Count);

  /// Canonical lowercase name of each Direction, indexed by value (also the sheet JSON key).
  inline constexpr std::array<std::string_view, k_num_directions> k_direction_names = {
      "south", "north", "east", "west", "northeast", "southeast", "southwest", "northwest",
  };

  /// Lookup table mapping a Direction to its opposite, indexed by Direction value.
  inline constexpr std::array<Direction, k_num_directions> k_opposite_direction = {
      Direction::North,     Direction::South,     Direction::West,      Direction::East,
      Direction::SouthWest, Direction::NorthWest, Direction::NorthEast, Direction::SouthEast,
  };

  static_assert(k_direction_names.size() == k_num_directions);
  static_assert(k_opposite_direction.size() == k_num_directions);

  /** @brief Direction directly opposite @p dir. @pre dir must not be Count. */
  [[nodiscard]] constexpr Direction opposite(Direction dir) noexcept {
    return k_opposite_direction[std::to_underlying(dir)];
  }

  /** @brief Canonical name of @p dir. @pre dir must not be Count. */
  [[nodiscard]] constexpr std::string_view direction_name(Direction dir) noexcept {
    return k_direction_names[std::to_underlying(dir)];
  }

  /** @brief Resolve a canonical direction name to a Direction; nullopt if unknown. */
  [[nodiscard]] constexpr std::optional<Direction> direction_from_name(std::string_view name) noexcept {
    for (uint8_t i = 0; i < k_num_directions; ++i)
      if (k_direction_names[i] == name)
        return static_cast<Direction>(i);
    return std::nullopt;
  }

  /** @brief Ratio above which the dominant tile axis counts as cardinal rather than diagonal. */
  inline constexpr float k_cardinal_dominance_ratio = 2.f;

  /** @brief The screen Direction nearest a tile-grid displacement.
   *
   *  The isometric projection rotates the grid 45° against the screen:
   *    tile SE (+,+) is screen South, tile NW (-,-) is North,
   *    tile NE (+,-) is East, tile SW (-,+) is West.
   *  @note Zero displacement gives North. */
  [[nodiscard]] inline Direction direction_from_tile_delta(float delta_col, float delta_row) noexcept {
    const float abs_col = std::abs(delta_col);
    const float abs_row = std::abs(delta_row);
    if (abs_row > k_cardinal_dominance_ratio * abs_col)
      return delta_row > 0.f ? Direction::SouthWest : Direction::NorthEast;
    if (abs_col > k_cardinal_dominance_ratio * abs_row)
      return delta_col > 0.f ? Direction::SouthEast : Direction::NorthWest;
    if (delta_col > 0.f)
      return delta_row > 0.f ? Direction::South : Direction::East;
    return delta_row > 0.f ? Direction::West : Direction::North;
  }

} // namespace corundum::core
