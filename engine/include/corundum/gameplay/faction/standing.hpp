// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <format>
#include <string>
#include <string_view>

// The framework keeps the player's standing with a faction under its own key, unlike the rest of
// the protagonist's lasting state, which a ruleset keeps under its prefix. A standing is a plain
// count: the `reputation` event adds to it, and nothing clamps it.
namespace corundum::gameplay::faction {

  /** @brief FlagStore key prefix of a faction's standing. */
  inline constexpr std::string_view k_standing_flag_prefix = "rep.";

  /** @brief FlagStore key holding the player's standing with @p faction: `rep.<faction>`. */
  [[nodiscard]] inline std::string standing_flag_key(std::string_view faction) {
    return std::format("{}{}", k_standing_flag_prefix, faction);
  }

} // namespace corundum::gameplay::faction
