// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <flat_map>
#include <format>
#include <functional>
#include <string>
#include <string_view>

namespace corundum::world {

/** Integer-count store unified for boolean flags and visit counters.
 *
 *  A flag set once has count 1, set twice has count 2; `has_flag` is true for
 *  any count >= 1. Internal dialogue keys are prefixed with '_'.
 *  Uses `std::flat_map` for cache-friendly sorted storage. The transparent `std::less<>` lets
 *  lookups take a `std::string_view` without building a key string.
 */
using FlagStore = std::flat_map<std::string, int, std::less<>>;

/** True if `name` has been set at least once.
 *  @param flags The flag store to query.
 *  @param name  Flag name.
 */
[[nodiscard]] inline bool has_flag(const FlagStore &flags, std::string_view name) noexcept {
  const auto it = flags.find(name);
  return it != flags.end() && it->second > 0;
}

/** Increment `name`'s count by one (initialises to 1 on first call).
 *  @param flags The flag store to modify.
 *  @param name  Flag name.
 */
inline void set_flag(FlagStore &flags, std::string_view name) {
  ++flags[name];
}

/** Remove `name` entirely (count returns to 0 / not present).
 *  @param flags The flag store to modify.
 *  @param name  Flag name.
 */
inline void clear_flag(FlagStore &flags, std::string_view name) {
  flags.erase(name);
}

/** Raw set count for `name`; 0 if never set.
 *  @param flags The flag store to query.
 *  @param name  Flag name.
 *  @return Current count, or 0 if absent.
 */
[[nodiscard]] inline int visit_count(const FlagStore &flags, std::string_view name) noexcept {
  const auto it = flags.find(name);
  return it != flags.end() ? it->second : 0;
}

/** The key prefix shared by every flag scoped to `zone_id`.
 *
 *  @param zone_id The zone id to build the prefix for.
 *  @return `zone.<zone_id>.`, the prefix `scoped_flag_key` embeds and `reset_zone` erases.
 */
[[nodiscard]] inline std::string zone_flag_prefix(std::string_view zone_id) {
  return std::format("zone.{}.", zone_id);
}

/** Resolve a `local.<key>` flag reference to its zone-scoped key.
 *
 *  `local.<key>` is authoring sugar for `zone.<zone_id>.<key>`. Bare keys and
 *  keys with any other prefix pass through unchanged. When `zone_id` is empty
 *  there is no active zone to scope into, so the reference is left as-is.
 *
 *  @param key     The flag key as written by content (`local.<key>` or bare).
 *  @param zone_id The current zone id; empty means no scoping.
 *  @return The FlagStore key to read or write.
 */
[[nodiscard]] inline std::string scoped_flag_key(std::string_view key, std::string_view zone_id) {
  constexpr std::string_view k_local_prefix = "local.";
  if (zone_id.empty() || !key.starts_with(k_local_prefix))
    return std::string(key);
  return std::format("{}{}", zone_flag_prefix(zone_id), key.substr(k_local_prefix.size()));
}

/** Erase every zone-scoped flag belonging to `zone_id` (`zone.<id>.<key>`).
 *
 *  Flags are stored in a sorted flat_map, so the erased keys form one
 *  contiguous range starting at the `zone.<id>.` prefix.
 *
 *  @param flags   The flag store to modify.
 *  @param zone_id The zone whose scoped keys should be removed.
 */
inline void reset_zone(FlagStore &flags, std::string_view zone_id) {
  const std::string prefix{zone_flag_prefix(zone_id)};
  const auto first = flags.lower_bound(prefix);
  auto last = first;
  while (last != flags.end() && last->first.starts_with(prefix))
    ++last;
  flags.erase(first, last);
}

} // namespace corundum::world
