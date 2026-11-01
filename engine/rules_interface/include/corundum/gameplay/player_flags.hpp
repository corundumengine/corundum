// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string_view>

// The framework's FlagStore keys that a game's ruleset reads. Part of corundum::rules_interface,
// which stays as small as the rulesets allow: a key moves here from its screen's header only when
// a ruleset needs it.
namespace corundum::gameplay {

  /** @brief FlagStore key holding the player's character level; defaults to 1 when absent. */
  inline constexpr std::string_view k_level_flag = "player.level";

} // namespace corundum::gameplay
