#pragma once

#include <flat_map>
#include <format>
#include <string>
#include <string_view>

namespace corundum::world {

  /** @brief Integer-count store unified for boolean flags and visit counters.
   *
   * A flag set once has count 1, set twice has count 2; `has_flag` is true for
   * any count >= 1. Internal dialogue keys are prefixed with '_'.
   * Uses `std::flat_map` for cache-friendly sorted storage.
   */
  using FlagStore = std::flat_map<std::string, int>;

  /** @brief True if @p name has been set at least once.
   *  @param[in] flags The flag store to query.
   *  @param[in] name  Flag name.
   *  @return True when the flag count is >= 1.
   */
  [[nodiscard]] inline bool has_flag(const FlagStore &flags, const std::string &name) noexcept {
    const auto it = flags.find(name);
    return it != flags.end() && it->second > 0;
  }

  /** @brief Increment @p name's count by one (initialises to 1 on first call).
   *  @param[in,out] flags The flag store to modify.
   *  @param[in]     name  Flag name.
   */
  inline void set_flag(FlagStore &flags, const std::string &name) {
    ++flags[name];
  }

  /** @brief Remove @p name entirely (count returns to 0 / not present).
   *  @param[in,out] flags The flag store to modify.
   *  @param[in]     name  Flag name.
   */
  inline void clear_flag(FlagStore &flags, const std::string &name) {
    flags.erase(name);
  }

  /** @brief Raw set count for @p name; 0 if never set.
   *  @param[in] flags The flag store to query.
   *  @param[in] name  Flag name.
   *  @return Current count, or 0 if absent.
   */
  [[nodiscard]] inline int visit_count(const FlagStore &flags, const std::string &name) noexcept {
    const auto it = flags.find(name);
    return it != flags.end() ? it->second : 0;
  }

  /** @brief Resolve a `local.<key>` flag reference to its zone-scoped key.
   *
   * `local.<key>` is authoring sugar for `zone.<zone_id>.<key>`. Bare keys and
   * keys with any other prefix pass through unchanged. When @p zone_id is empty
   * there is no active zone to scope into, so the reference is left as-is.
   *
   * @param key     The flag key as written by content (`local.<key>` or bare).
   * @param zone_id The current zone id; empty means no scoping.
   * @return The FlagStore key to read or write.
   */
  [[nodiscard]] inline std::string scoped_flag_key(std::string_view key, std::string_view zone_id) {
    constexpr std::string_view k_local_prefix = "local.";
    if (zone_id.empty() || !key.starts_with(k_local_prefix))
      return std::string(key);
    return std::format("zone.{}.{}", zone_id, key.substr(k_local_prefix.size()));
  }

  /** @brief Erase every zone-scoped flag belonging to @p zone_id (`zone.<id>.<key>`).
   *
   * Flags are stored in a sorted flat_map, so the erased keys form one
   * contiguous range starting at the `zone.<id>.` prefix.
   *
   * @param[in,out] flags   The flag store to modify.
   * @param[in]     zone_id The zone whose scoped keys should be removed.
   */
  inline void reset_zone(FlagStore &flags, std::string_view zone_id) {
    const std::string prefix = std::format("zone.{}.", zone_id);
    auto it = flags.lower_bound(prefix);
    while (it != flags.end() && it->first.starts_with(prefix))
      it = flags.erase(it);
  }

} // namespace corundum::world
