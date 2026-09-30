// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include "warn_log.hpp"

#include <format>
#include <stdexcept>
#include <utility>

namespace {
  struct ThrowsOnFormat {};
} // namespace

template <> struct std::formatter<ThrowsOnFormat> {
  static constexpr auto parse(std::format_parse_context &context) {
    return context.begin();
  }

  static auto format(const ThrowsOnFormat & /*value*/, std::format_context & /*context*/)
      -> std::format_context::iterator {
    throw std::runtime_error("formatter failure");
  }
};

TEST_CASE("warn_log and info_log are noexcept") {
  static_assert(noexcept(corundum::detail::warn_log(std::declval<std::format_string<int>>(), 1)));
  static_assert(noexcept(corundum::detail::info_log(std::declval<std::format_string<int>>(), 1)));
}

TEST_CASE("warn_log swallows an exception thrown while formatting") {
  CHECK_NOTHROW(corundum::detail::warn_log("{}", ThrowsOnFormat{}));
}

TEST_CASE("info_log swallows an exception thrown while formatting") {
  CHECK_NOTHROW(corundum::detail::info_log("{}", ThrowsOnFormat{}));
}
