// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <format>
#include <string>
#include <string_view>

// The framework's FlagStore keys that a game's ruleset reads. Part of corundum::rules_interface,
// which stays as small as the rulesets allow: a key moves here from its screen's header only when
// a ruleset needs it.
namespace corundum::gameplay {

  /** @brief FlagStore key holding the player's character level; defaults to 1 when absent. */
  inline constexpr std::string_view k_level_flag = "player.level";

  /** @brief Equipment slot every weapon occupies. Apparel names its own slot; weapons carry no
   *  slot field, so they all share this one. */
  inline constexpr std::string_view k_weapon_slot = "weapon";

  /** @brief FlagStore key marking @p item_id equipped in @p slot: `equip.<slot>.<item id>`. */
  [[nodiscard]] inline std::string equip_flag_key(std::string_view slot, std::string_view item_id) {
    return std::format("equip.{}.{}", slot, item_id);
  }

} // namespace corundum::gameplay
