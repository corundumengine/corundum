// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <corundum/gameplay/rules/rules.hpp>
#include <corundum/world/flags.hpp>

#include <string_view>

namespace corundum::gameplay {

  /** @brief A ruleset's read view of @p flags; valid only for the duration of the call it is
   *  passed to. */
  [[nodiscard]] inline rules::FlagReader make_flag_reader(const world::FlagStore &flags) {
    return rules::FlagReader{
        .read = [&flags](std::string_view key) { return world::visit_count(flags, key); },
    };
  }

  /** @brief A ruleset's write view of @p flags; valid only for the duration of the call it is
   *  passed to. */
  [[nodiscard]] inline rules::FlagWriter make_flag_writer(world::FlagStore &flags) {
    return rules::FlagWriter{
        .write = [&flags](std::string_view key, int value) { flags[key] = value; },
    };
  }

} // namespace corundum::gameplay
