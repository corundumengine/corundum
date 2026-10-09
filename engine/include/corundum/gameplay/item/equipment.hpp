// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <corundum/gameplay/item/item.hpp>
#include <corundum/gameplay/item/registry.hpp>
#include <corundum/world/flags.hpp>

#include <expected>
#include <string>
#include <string_view>

namespace corundum::gameplay::item {

  /** @brief Slot name weapons occupy when they carry no apparel slot.
   *
   *  Apparel's slot vocabulary is author-defined (`ApparelData::slot`); weapons have no slot
   *  field, so they all share this one. */
  inline constexpr std::string_view k_weapon_slot = "weapon";

  /** @brief The equipment slot @p item occupies, or empty when it cannot be equipped.
   *
   *  An apparel item with a non-empty slot uses that slot; a weapon uses k_weapon_slot;
   *  everything else (misc, potions, armour with a blank slot) is not equippable.
   */
  [[nodiscard]] std::string equipment_slot(const Item &item);

  /** @brief FlagStore key marking @p item_id equipped in @p slot: `equip.<slot>.<item id>`. */
  [[nodiscard]] std::string equip_flag_key(std::string_view slot, std::string_view item_id);

  /** @brief True when the held item @p item_id is equipped in its own slot.
   *
   *  @pre @p items holds @p item_id (an unknown id returns false).
   */
  [[nodiscard]] bool is_equipped(const world::FlagStore &flags, const Registry &items, std::string_view item_id);

  /** @brief Equip the held item @p item_id, clearing whichever item occupied its slot.
   *
   *  @param[in,out] flags    FlagStore holding the inventory and the `equip.` flags.
   *  @param[in]     items    Item registry resolving @p item_id's slot.
   *  @param[in]     item_id  Id of a held, equippable item.
   *  @return ok, or why the item could not be equipped (unknown, not held, not equippable).
   *  @post The item's slot holds only @p item_id.
   */
  [[nodiscard]] std::expected<void, std::string> equip_item(world::FlagStore &flags, const Registry &items,
                                                            std::string_view item_id);

  /** @brief Unequip the held item @p item_id.
   *
   *  @return ok, or why the item could not be unequipped (unknown, not equippable, not equipped).
   */
  [[nodiscard]] std::expected<void, std::string> unequip_item(world::FlagStore &flags, const Registry &items,
                                                              std::string_view item_id);

  /** @brief Unequip @p item_id when equipped, equip it otherwise. */
  [[nodiscard]] std::expected<void, std::string> toggle_equip(world::FlagStore &flags, const Registry &items,
                                                              std::string_view item_id);

} // namespace corundum::gameplay::item
