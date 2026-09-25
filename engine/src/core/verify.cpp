// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/verify.hpp>

#include "core/warn_log.hpp"

#include <cstdlib>
#include <source_location>
#include <string_view>

namespace corundum::core {

  void verify_failed(std::string_view message, std::source_location where) noexcept {
    corundum::detail::warn_log("[corundum] FATAL: {} ({}:{} in {})", message, where.file_name(), where.line(),
                               where.function_name());
    std::abort();
  }

} // namespace corundum::core
