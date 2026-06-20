#pragma once
#include <print>
#include <source_location>
#include <stacktrace>
#include <string_view>

namespace corundum::core::memory {

  /** @brief Log an allocation failure with location and stack trace.
   *
   * Intended for use in debug builds to diagnose unexpected allocation failures
   * during frame updates. Prints to stderr and calls std::terminate in debug
   * builds so the failure is visible immediately.
   *
   * @param msg       Human-readable description of the failure.
   * @param loc       Source location of the call site (default: caller).
   *
   * @code
   *   auto result = pool.acquire();
   *   if (!result)
   *     log_allocation_failure(result.error());
   * @endcode
   */
  [[noreturn]] inline void
  log_allocation_failure(std::string_view msg,
                         const std::source_location &loc = std::source_location::current()) noexcept {
    std::println(stderr, "[alloc] FAILURE at {}:{} ({}): {}", loc.file_name(), loc.line(), loc.function_name(), msg);
    std::println(stderr, "{}", std::stacktrace::current());
    std::terminate();
  }

} // namespace corundum::core::memory
