// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
// Item counts in the FlagStore: the key that holds the player's count of an item, and the
// add-or-remove step the loot and barter screens use to move items between holders.

#include <corundum/gameplay/item/item.hpp>
#include <corundum/world/flags.hpp>

#include <format>
#include <string>
#include <string_view>

namespace corundum::gameplay {

/** The FlagStore key holding an item's runtime count (`item.<id>`). */
inline std::string item_flag_key(std::string_view id) {
  return std::format("{}{}", item::k_flag_prefix, id);
}

/** Add `delta` to `key`'s count, creating it when positive and erasing it once it reaches
 *  zero. Keeps the FlagStore free of zero-valued rows (matching item grant/take semantics).
 */
inline void adjust_flag(world::FlagStore &flags, const std::string &key, int delta) {
  if (delta == 0)
    return;
  const auto it = flags.find(key);
  if (it == flags.end()) {
    if (delta > 0)
      flags.emplace(key, delta);
    return;
  }
  it->second += delta;
  if (it->second <= 0)
    flags.erase(it);
}

} // namespace corundum::gameplay
