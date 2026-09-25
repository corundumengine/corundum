// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <source_location>
#include <string_view>

namespace corundum::core {

  /** @brief Report a failed verify() and abort the process.
   *
   * Out of line so verify() inlines to a compare-and-branch and this header pulls in no I/O.
   * Call verify(), not this.
   */
  [[noreturn]] void verify_failed(std::string_view message, std::source_location where) noexcept;

  /** @brief Check an invariant whose violation would corrupt memory, in every build type.
   *
   * Unlike assert(), survives NDEBUG: on failure it logs @p message and the caller's location
   * to stderr, then aborts. Reserve it for cheap checks that guard an out-of-bounds write (pool
   * exhaustion, fixed-buffer overflow); ordinary preconditions stay assert().
   */
  inline void verify(bool condition, std::string_view message,
                     std::source_location where = std::source_location::current()) noexcept {
    if (!condition) [[unlikely]]
      verify_failed(message, where);
  }

} // namespace corundum::core
