#pragma once

#include <flat_map>
#include <string>

namespace corundum::gameplay {

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
    auto it = flags.find(name);
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
    auto it = flags.find(name);
    return it != flags.end() ? it->second : 0;
  }

} // namespace corundum::gameplay
