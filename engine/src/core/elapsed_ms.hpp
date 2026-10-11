// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <chrono>
#include <ratio>

namespace corundum::detail {

  /// Milliseconds from @p start to now, for boot-phase log lines.
  [[nodiscard]] inline double elapsed_ms(std::chrono::steady_clock::time_point start) noexcept {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  }

} // namespace corundum::detail
