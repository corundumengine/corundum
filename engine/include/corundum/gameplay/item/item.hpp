// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <corundum/gameplay/player_flags.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace corundum::gameplay::item {

/** Current on-disk item batch file format version; every batch file must declare it. */
inline constexpr int k_item_schema_version = 1;

/** What kind of thing an item is; drives inventory grouping. */
enum class ItemCategory : uint8_t { Apparel, Misc, Potion, Weapon };

/** Parse a category name ("weapon", "apparel", ...). Unknown or absent → Misc. */
[[nodiscard]] ItemCategory category_from_string(std::string_view name) noexcept;

/** Canonical serialized form of a category, matching the JSON schema enum. */
[[nodiscard]] std::string_view to_string(ItemCategory category) noexcept;

/** On-disk directory name for a category's batch files under data/items/. */
[[nodiscard]] std::string_view category_dir_name(ItemCategory category) noexcept;

/** Parse a category directory name ("weapons", "apparel", ...). Unknown → nullopt. */
[[nodiscard]] std::optional<ItemCategory> category_from_dir_name(std::string_view name) noexcept;

/** FlagStore key prefix for an item's runtime count: `item.<id>`. */
inline constexpr std::string_view k_flag_prefix = k_item_flag_prefix;

/** True when `flag_key` names a held item and `count` is positive.
 *
 *  Sole authority for which FlagStore rows the inventory treats as held items;
 *  both the row count (cursor wrapping) and the rendered list go through it.
 */
[[nodiscard]] constexpr bool is_held_item(std::string_view flag_key, int count) noexcept {
  return flag_key.starts_with(k_flag_prefix) && count > 0;
}

/** Strip the `item.` prefix from `flag_key`.
 *  @pre is_held_item(flag_key, count) returned true for `flag_key`.
 */
[[nodiscard]] constexpr std::string_view item_id_from_flag(std::string_view flag_key) noexcept {
  flag_key.remove_prefix(k_flag_prefix.size());
  return flag_key;
}

/** Category payload for clothing/armour. */
struct ApparelData {
  int defense = 0;
  std::string slot; // free-form for now (e.g. "head", "body") — no equip system exists
                    // yet to define a closed slot set.
};

/** Category payload for consumables. */
struct PotionData {
  std::string effect; // free-form identifier, e.g. "heal" — read by game code via

  /** on_event/on_fixed_update, same convention as the existing
   *  "give_item"/"reputation" EventActions.
   */
  int magnitude = 0;
};

/** Category payload for weapons. */
struct WeaponData {
  int damage = 0;
};

/** A static item definition loaded from data/items/<category>/<file>.json.
 *
 *  The category folder is authoritative: the loader injects Item::category from it.
 *  The filename inside a category folder is author-chosen (grouped however the
 *  author likes) and carries no meaning the engine parses.
 *  Runtime quantity is tracked separately as an "item.<id>" count in the FlagStore.
 */
struct Item {
  /** Populated when category is Apparel. */
  std::optional<ApparelData> apparel{};
  ItemCategory category = ItemCategory::Misc;

  /** Flavor / tooltip text. */
  std::string description{};

  /** Optional icon reference (unused in MVP; reserved). */
  std::string icon{};

  /** Unique key. */
  std::string id{};

  /** Display name. */
  std::string name{};

  /** Populated when category is Potion. */
  std::optional<PotionData> potion{};

  /** Base gold value; 0 means not for sale. Shops may override per stock entry. */
  int price{};

  /** Populated when category is Weapon. */
  std::optional<WeaponData> weapon{};
};

} // namespace corundum::gameplay::item
