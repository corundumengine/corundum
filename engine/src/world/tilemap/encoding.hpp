// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/world/tilemap/tilemap.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

namespace corundum::world::tilemap {

  /** @brief JSON spellings for the tilemap cell-encoding enums.
   *
   * Single source of truth shared by the loader and serializer so the on-disk
   * spelling of a cell attribute is defined in exactly one place.
   */
  [[nodiscard]] constexpr std::string_view to_string(TriangleCut cut) noexcept {
    switch (cut) {
      case TriangleCut::NorthWest:
        return "NW";
      case TriangleCut::NorthEast:
        return "NE";
      case TriangleCut::SouthWest:
        return "SW";
      case TriangleCut::SouthEast:
        return "SE";
    }
    return {};
  }

  /** @brief Parse a collision-triangle `cut` value; nullopt when unrecognized. */
  [[nodiscard]] constexpr std::optional<TriangleCut> triangle_cut_from_string(std::string_view text) noexcept {
    if (text == "NW")
      return TriangleCut::NorthWest;
    if (text == "NE")
      return TriangleCut::NorthEast;
    if (text == "SW")
      return TriangleCut::SouthWest;
    if (text == "SE")
      return TriangleCut::SouthEast;
    return std::nullopt;
  }

  /** @brief JSON spelling for a ramp axis. */
  [[nodiscard]] constexpr std::string_view to_string(RampAxis axis) noexcept {
    return axis == RampAxis::NorthSouth ? "ns" : "ew";
  }

  /** @brief Parse a ramp `axis` value; nullopt when unrecognized. */
  [[nodiscard]] constexpr std::optional<RampAxis> ramp_axis_from_string(std::string_view text) noexcept {
    if (text == "ns")
      return RampAxis::NorthSouth;
    if (text == "ew")
      return RampAxis::EastWest;
    return std::nullopt;
  }

  /** @brief JSON spelling for a flip-flag byte; empty when no flip is set. */
  [[nodiscard]] constexpr std::string_view to_string(uint8_t flip_flags) noexcept {
    if (flip_flags == (k_flip_h | k_flip_v))
      return "HV";
    if (flip_flags == k_flip_h)
      return "H";
    if (flip_flags == k_flip_v)
      return "V";
    return {};
  }

  /** @brief Parse a `flip` value; nullopt when unrecognized. */
  [[nodiscard]] constexpr std::optional<uint8_t> flip_flags_from_string(std::string_view text) noexcept {
    if (text == "H")
      return k_flip_h;
    if (text == "V")
      return k_flip_v;
    if (text == "HV")
      return static_cast<uint8_t>(k_flip_h | k_flip_v);
    return std::nullopt;
  }

} // namespace corundum::world::tilemap
