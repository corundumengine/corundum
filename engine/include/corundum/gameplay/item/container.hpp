// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <format>
#include <string>
#include <string_view>

namespace corundum::gameplay::item {

  /** @brief FlagStore key prefix scoping one container's contents: `container.<id>.item.`.
   *
   *  A container's contents are stored under this prefix so a chest and the player can hold
   *  the same item independently; the plain `item.` prefix remains the player's inventory. */
  [[nodiscard]] inline std::string container_flag_prefix(std::string_view container_id) {
    return std::format("container.{}.item.", container_id);
  }

  /** @brief FlagStore key holding @p item_id's count inside @p container_id. */
  [[nodiscard]] inline std::string container_item_flag_key(std::string_view container_id, std::string_view item_id) {
    return std::format("container.{}.item.{}", container_id, item_id);
  }

} // namespace corundum::gameplay::item
