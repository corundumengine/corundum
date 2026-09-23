// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdio>
#include <format>
#include <print>
#include <utility>

namespace corundum::detail {

  /// stderr logging that survives in noexcept contexts — std::println can throw
  /// bad_alloc / length_error on allocation failure, so swallow to preserve the
  /// caller's noexcept contract (best-effort logging).
  template <typename... Args> void warn_log(std::format_string<Args...> fmt, Args &&...args) noexcept {
    try {
      std::println(stderr, fmt, std::forward<Args>(args)...);
    } catch (...) {
      return;
    }
  }

} // namespace corundum::detail
