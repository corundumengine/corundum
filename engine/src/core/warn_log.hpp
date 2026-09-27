// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdio>
#include <format>
#include <print>
#include <utility>

namespace corundum::detail {

  /// Best-effort log line that survives in noexcept contexts. std::println can throw
  /// (bad_alloc while formatting, system_error when the write fails), so the exception is
  /// swallowed to preserve the caller's noexcept contract: a lost line beats std::terminate.
  template <typename... Args>
  void log_line(std::FILE *stream, std::format_string<Args...> fmt, Args &&...args) noexcept {
    try {
      std::println(stream, fmt, std::forward<Args>(args)...);
    } catch (...) {
      return;
    }
  }

  /// Warnings and errors, to stderr.
  template <typename... Args> void warn_log(std::format_string<Args...> fmt, Args &&...args) noexcept {
    log_line(stderr, fmt, std::forward<Args>(args)...);
  }

  /// Progress and informational lines, to stdout.
  template <typename... Args> void info_log(std::format_string<Args...> fmt, Args &&...args) noexcept {
    log_line(stdout, fmt, std::forward<Args>(args)...);
  }

} // namespace corundum::detail
