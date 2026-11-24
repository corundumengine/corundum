// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <corundum/gameplay/item/item.hpp>
#include <corundum/gameplay/item/registry.hpp>
#include <corundum/gameplay/player_flags.hpp> // IWYU pragma: export
#include <corundum/world/flags.hpp>

#include <expected>
#include <string>
#include <string_view>

namespace corundum::gameplay::item {

/** The key spelling is part of the rules interface (player_flags.hpp), so a ruleset can read
 *  what is equipped; item code keeps naming it item::.
 */
using gameplay::equip_flag_key;
using gameplay::k_weapon_slot;

/** The equipment slot `item` occupies, or empty when it cannot be equipped.
 *
 *  An apparel item with a non-empty slot uses that slot; a weapon uses k_weapon_slot;
 *  everything else (misc, potions, armour with a blank slot) is not equippable.
 */
[[nodiscard]] std::string equipment_slot(const Item &item);

/** True when the held item `item_id` is equipped in its own slot.
 *
 *  @pre `items` holds `item_id` (an unknown id returns false).
 */
[[nodiscard]] bool is_equipped(const world::FlagStore &flags, const Registry &items, std::string_view item_id);

/** Equip the held item `item_id`, clearing whichever item occupied its slot.
 *
 *  @param flags    FlagStore holding the inventory and the `equip.` flags.
 *  @param items    Item registry resolving `item_id`'s slot.
 *  @param item_id  Id of a held, equippable item.
 *  @return ok, or why the item could not be equipped (unknown, not held, not equippable).
 *  @post The item's slot holds only `item_id`.
 */
[[nodiscard]] std::expected<void, std::string> equip_item(world::FlagStore &flags, const Registry &items,
                                                          std::string_view item_id);

/** Unequip the held item `item_id`.
 *
 *  @return ok, or why the item could not be unequipped (unknown, not equippable, not equipped).
 */
[[nodiscard]] std::expected<void, std::string> unequip_item(world::FlagStore &flags, const Registry &items,
                                                            std::string_view item_id);

/** Unequip `item_id` when equipped, equip it otherwise. */
[[nodiscard]] std::expected<void, std::string> toggle_equip(world::FlagStore &flags, const Registry &items,
                                                            std::string_view item_id);

} // namespace corundum::gameplay::item
