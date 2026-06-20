#pragma once
#include <print>

namespace corundum::core::log {

  /// @brief Log an informational message to stdout.
  template <typename... Args> void info(std::format_string<Args...> fmt, Args &&...args) {
    std::println(stdout, fmt, std::forward<Args>(args)...);
  }

  /// @brief Log a warning message to stderr.
  template <typename... Args> void warn(std::format_string<Args...> fmt, Args &&...args) {
    std::println(stderr, "[warn] {}", std::format(fmt, std::forward<Args>(args)...));
  }

  /// @brief Log an error message to stderr.
  template <typename... Args> void error(std::format_string<Args...> fmt, Args &&...args) {
    std::println(stderr, "[error] {}", std::format(fmt, std::forward<Args>(args)...));
  }

} // namespace corundum::core::log
