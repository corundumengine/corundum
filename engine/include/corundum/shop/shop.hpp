// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <vector>

namespace corundum::shop {

  /** @brief Current on-disk shop batch file format version; every batch file must declare it. */
  inline constexpr int k_shop_schema_version = 1;

  /** @brief One item a shop offers, with an optional price override.
   *
   *  A @c price of 0 means "use the item definition's own price". */
  struct StockEntry {
    std::string item{}; ///< Item id.
    int price{};        ///< Base buy price; 0 falls back to Item::price.
  };

  /** @brief A merchant definition authored in data/shops/<file>.json. */
  struct Shop {
    float buy_rate{0.5f};            ///< Fraction of base price paid when buying from the player.
    std::string faction{};           ///< Reputation faction key (`rep.<faction>`); empty means no discount.
    std::string id{};                ///< Unique key.
    std::string name{};              ///< Display heading.
    std::vector<StockEntry> stock{}; ///< Items offered for sale.
  };

} // namespace corundum::shop
