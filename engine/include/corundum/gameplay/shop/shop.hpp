// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <vector>

namespace corundum::gameplay::shop {

/** Current on-disk shop batch file format version; every batch file must declare it. */
inline constexpr int k_shop_schema_version = 1;

/** One item a shop offers, with an optional price override.
 *
 *  Stock is static by design: the quantity a merchant carries is not tracked or decremented
 *  when the player buys, so a shop's inventory is effectively infinite.
 *
 *  A `price` of 0 means "use the item definition's own price".
 */
struct StockEntry {
  std::string item{}; ///< Item id.
  int price{};        ///< Unit price; 0 falls back to Item::price.
};

/** A merchant definition authored in data/shops/<file>.json. */
struct Shop {
  std::string id{};                ///< Unique key.
  std::string name{};              ///< Display heading.
  std::vector<StockEntry> stock{}; ///< Items offered for sale.
};

} // namespace corundum::gameplay::shop
