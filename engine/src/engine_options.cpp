// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/engine_factory.hpp>

#include <span>
#include <string_view>

namespace corundum {

  EngineOptions parse_engine_args(std::span<const char *const> args) {
    EngineOptions options{};
    for (const char *const arg : args) {
      if (arg != nullptr && std::string_view(arg) == "--debug")
        options.show_debug_hud = true;
    }
    return options;
  }

} // namespace corundum
