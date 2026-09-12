// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <array>
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

} // namespace corundum::core
