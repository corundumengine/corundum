// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <format>
#include <string>
#include <string_view>

namespace corundum::gameplay::location {

/** Current on-disk location batch file format version; every batch file must declare it. */
inline constexpr int k_location_schema_version = 1;

/** FlagStore key prefix marking a location as discovered: `location.<id>.discovered`. */
inline constexpr std::string_view k_flag_prefix = "location.";

/** FlagStore key marking location `id` discovered. */
[[nodiscard]] inline std::string discovery_flag_key(std::string_view id) {
  return std::format("{}{}.discovered", k_flag_prefix, id);
}

/** A fast-travel destination authored in data/locations/<file>.json.
 *
 *  A location either names a single tilemap (`map` non-empty) or the streamed overworld
 *  (`map` empty), in which case travel re-enters the world at (`col`, `row`). It is hidden
 *  from the map screen until its discovery flag is set.
 */
struct Location {
  /** Spawn tile column in the destination. */
  float col{};

  /** Unique key; also the suffix of the discovery flag. */
  std::string id{};

  /** Target tilemap path; empty means the overworld. */
  std::string map{};

  /** Display heading. */
  std::string name{};

  /** True to re-enter the overworld instead of loading `map`. */
  bool return_to_world{};

  /** Spawn tile row in the destination. */
  float row{};

  /** Zone id to force after travelling; empty derives it. */
  std::string zone{};
};

} // namespace corundum::gameplay::location
