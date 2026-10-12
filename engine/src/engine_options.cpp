// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/engine_factory.hpp>

#include <span>
#include <string_view>

namespace corundum {

  EngineOptions parse_engine_args(std::span<const char *const> args) {
    EngineOptions options{};
    for (const char *const arg : args) {
      if (arg == nullptr)
        continue;
      const std::string_view flag{arg};
      if (flag == "--debug")
        options.show_debug_hud = true;
      else if (flag == "--no-splash")
        options.show_splash = false;
    }
    return options;
  }

} // namespace corundum
