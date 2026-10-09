// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/gameplay/item/equipment.hpp>

#include <corundum/gameplay/item/item.hpp>
#include <corundum/gameplay/item/registry.hpp>
#include <corundum/world/flags.hpp>

#include <expected>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace corundum::gameplay::item {

  namespace {

    /// The held-item count key for @p item_id (`item.<id>`).
    std::string held_flag_key(std::string_view item_id) {
      return std::format("{}{}", k_flag_prefix, item_id);
    }

    /// The name to show in an error line; falls back to the raw id when the definition is absent.
    std::string display_name(const Item *item, std::string_view item_id) {
      return item != nullptr && !item->name.empty() ? item->name : std::string{item_id};
    }

  } // namespace

  std::string equipment_slot(const Item &item) {
    if (item.apparel && !item.apparel->slot.empty())
      return item.apparel->slot;
    if (item.weapon)
      return std::string{k_weapon_slot};
    return {};
  }

  std::string equip_flag_key(std::string_view slot, std::string_view item_id) {
    return std::format("equip.{}.{}", slot, item_id);
  }

  bool is_equipped(const world::FlagStore &flags, const Registry &items, std::string_view item_id) {
    const Item *item = items.find(item_id);
    if (item == nullptr)
      return false;
    const std::string slot = equipment_slot(*item);
    return !slot.empty() && world::has_flag(flags, equip_flag_key(slot, item_id));
  }

  std::expected<void, std::string> equip_item(world::FlagStore &flags, const Registry &items,
                                              std::string_view item_id) {
    const Item *item = items.find(item_id);
    const std::string slot = item != nullptr ? equipment_slot(*item) : std::string{};
    if (slot.empty())
      return std::unexpected(std::format("{} cannot be equipped", display_name(item, item_id)));
    if (world::visit_count(flags, held_flag_key(item_id)) <= 0)
      return std::unexpected(std::format("{} is not in the inventory", display_name(item, item_id)));

    const std::string equipped_key = equip_flag_key(slot, item_id);
    const std::string slot_prefix = std::format("equip.{}.", slot);

    // Collect first, mutate after: clearing while iterating the flat_map would invalidate the
    // iterator. Clearing by slot prefix (not by held item) also drops a stale flag left behind
    // when the previous occupant left the inventory without being unequipped.
    std::vector<std::string> stale_keys;
    for (const auto &[key, count] : flags) {
      if (count > 0 && key != equipped_key && key.starts_with(slot_prefix))
        stale_keys.push_back(key);
    }
    for (const std::string &key : stale_keys)
      world::clear_flag(flags, key);

    // Assign rather than set_flag(): re-equipping an already-equipped item must stay at 1.
    flags[equipped_key] = 1;
    return {};
  }

  std::expected<void, std::string> unequip_item(world::FlagStore &flags, const Registry &items,
                                                std::string_view item_id) {
    const Item *item = items.find(item_id);
    const std::string slot = item != nullptr ? equipment_slot(*item) : std::string{};
    if (slot.empty())
      return std::unexpected(std::format("{} cannot be equipped", display_name(item, item_id)));

    const std::string key = equip_flag_key(slot, item_id);
    if (!world::has_flag(flags, key))
      return std::unexpected(std::format("{} is not equipped", display_name(item, item_id)));
    world::clear_flag(flags, key);
    return {};
  }

  std::expected<void, std::string> toggle_equip(world::FlagStore &flags, const Registry &items,
                                                std::string_view item_id) {
    if (is_equipped(flags, items, item_id))
      return unequip_item(flags, items, item_id);
    return equip_item(flags, items, item_id);
  }

} // namespace corundum::gameplay::item
