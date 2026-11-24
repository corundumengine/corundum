// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <corundum/gameplay/rules/rules.hpp>
#include <corundum/world/flags.hpp>

#include <string_view>

namespace corundum::gameplay {

/** A ruleset's read view of `flags`; valid only for the duration of the call it is
 *  passed to.
 */
[[nodiscard]] inline rules::FlagReader make_flag_reader(const world::FlagStore &flags) {
  return rules::FlagReader{
      .read = [&flags](std::string_view key) { return world::visit_count(flags, key); },
  };
}

} // namespace corundum::gameplay
