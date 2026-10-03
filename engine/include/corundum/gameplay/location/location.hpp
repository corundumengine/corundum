// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <format>
#include <string>
#include <string_view>

namespace corundum::gameplay::location {

  /** @brief Current on-disk location batch file format version; every batch file must declare it. */
  inline constexpr int k_location_schema_version = 1;

  /** @brief FlagStore key prefix marking a location as discovered: `location.<id>.discovered`. */
  inline constexpr std::string_view k_flag_prefix = "location.";

  /** @brief FlagStore key marking location @p id discovered. */
  [[nodiscard]] inline std::string discovery_flag_key(std::string_view id) {
    return std::format("{}{}.discovered", k_flag_prefix, id);
  }

  /** @brief A fast-travel destination authored in data/locations/<file>.json.
   *
   *  A location either names a single tilemap (`map` non-empty) or the streamed overworld
   *  (`map` empty), in which case travel re-enters the world at (@c col, @c row). It is hidden
   *  from the map screen until its discovery flag is set. */
  struct Location {
    float col{};            ///< Spawn tile column in the destination.
    std::string id{};       ///< Unique key; also the suffix of the discovery flag.
    std::string map{};      ///< Target tilemap path; empty means the overworld.
    std::string name{};     ///< Display heading.
    bool return_to_world{}; ///< True to re-enter the overworld instead of loading `map`.
    float row{};            ///< Spawn tile row in the destination.
    std::string zone{};     ///< Zone id to force after travelling; empty derives it.
  };

} // namespace corundum::gameplay::location
