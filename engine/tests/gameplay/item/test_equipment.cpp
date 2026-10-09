// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/gameplay/item/equipment.hpp>
#include <corundum/gameplay/item/item.hpp>
#include <corundum/gameplay/item/registry.hpp>
#include <corundum/world/flags.hpp>

#include <expected>
#include <string>

namespace {

  using corundum::gameplay::item::ApparelData;
  using corundum::gameplay::item::Item;
  using corundum::gameplay::item::ItemCategory;
  using corundum::gameplay::item::Registry;
  using corundum::gameplay::item::WeaponData;
  using corundum::world::FlagStore;

  /// Two body items (to exercise slot replacement), one weapon, and one misc item.
  Registry make_items() {
    Registry items;
    items.add(Item{
        .apparel = ApparelData{.defense = 1, .slot = "body"},
        .category = ItemCategory::Apparel,
        .id = "leather_armor",
        .name = "Leather Armor",
    });
    items.add(Item{
        .apparel = ApparelData{.defense = 2, .slot = "body"},
        .category = ItemCategory::Apparel,
        .id = "steel_armor",
        .name = "Steel Armor",
    });
    items.add(
        Item{.category = ItemCategory::Weapon, .id = "sword", .name = "Sword", .weapon = WeaponData{.damage = 3}});
    items.add(Item{.category = ItemCategory::Misc, .id = "pebble", .name = "Pebble"});
    return items;
  }

} // namespace

TEST_CASE("equipment_slot: apparel uses its slot, a weapon uses weapon, misc has none") {
  const Registry items = make_items();

  CHECK(corundum::gameplay::item::equipment_slot(*items.find("leather_armor")) == "body");
  CHECK(corundum::gameplay::item::equipment_slot(*items.find("sword")) == "weapon");
  CHECK(corundum::gameplay::item::equipment_slot(*items.find("pebble")).empty());
}

TEST_CASE("equip_flag_key: builds the documented equip.<slot>.<id> spelling") {
  CHECK(corundum::gameplay::item::equip_flag_key("body", "cloak") == "equip.body.cloak");
}

TEST_CASE("equip_item: sets the item's slot flag and is idempotent") {
  const Registry items = make_items();
  FlagStore flags;
  flags["item.sword"] = 1;

  const std::expected<void, std::string> first = corundum::gameplay::item::equip_item(flags, items, "sword");
  REQUIRE(first.has_value());
  CHECK(flags["equip.weapon.sword"] == 1);

  // Equipping again must not accumulate the count into a phantom "equipped twice".
  REQUIRE(corundum::gameplay::item::equip_item(flags, items, "sword").has_value());
  CHECK(flags["equip.weapon.sword"] == 1);
}

TEST_CASE("equip_item: equipping a second item in the same slot clears the first") {
  const Registry items = make_items();
  FlagStore flags;
  flags["item.leather_armor"] = 1;
  flags["item.steel_armor"] = 1;
  REQUIRE(corundum::gameplay::item::equip_item(flags, items, "leather_armor").has_value());

  REQUIRE(corundum::gameplay::item::equip_item(flags, items, "steel_armor").has_value());

  CHECK(corundum::world::has_flag(flags, "equip.body.steel_armor"));
  CHECK_FALSE(corundum::world::has_flag(flags, "equip.body.leather_armor"));
}

TEST_CASE("equip_item: clears a stale slot flag left by an item that left the inventory") {
  const Registry items = make_items();
  FlagStore flags;
  flags["item.leather_armor"] = 1;
  REQUIRE(corundum::gameplay::item::equip_item(flags, items, "leather_armor").has_value());

  // The armour leaves the inventory without being unequipped first.
  corundum::world::clear_flag(flags, "item.leather_armor");
  flags["item.steel_armor"] = 1;
  REQUIRE(corundum::gameplay::item::equip_item(flags, items, "steel_armor").has_value());

  CHECK(corundum::world::has_flag(flags, "equip.body.steel_armor"));
  CHECK_FALSE(corundum::world::has_flag(flags, "equip.body.leather_armor"));
}

TEST_CASE("equip_item: rejects an unknown, unheld or unequippable item with a message") {
  const Registry items = make_items();
  FlagStore flags;

  const auto unknown = corundum::gameplay::item::equip_item(flags, items, "ghost");
  REQUIRE_FALSE(unknown.has_value());
  CHECK(unknown.error() == "ghost cannot be equipped");

  const auto unheld = corundum::gameplay::item::equip_item(flags, items, "sword");
  REQUIRE_FALSE(unheld.has_value());
  CHECK(unheld.error() == "Sword is not in the inventory");

  flags["item.pebble"] = 1;
  const auto unequippable = corundum::gameplay::item::equip_item(flags, items, "pebble");
  REQUIRE_FALSE(unequippable.has_value());
  CHECK(unequippable.error() == "Pebble cannot be equipped");
}

TEST_CASE("unequip_item: clears the slot flag and rejects an item that is not equipped") {
  const Registry items = make_items();
  FlagStore flags;
  flags["item.sword"] = 1;
  REQUIRE(corundum::gameplay::item::equip_item(flags, items, "sword").has_value());

  REQUIRE(corundum::gameplay::item::unequip_item(flags, items, "sword").has_value());
  CHECK_FALSE(corundum::world::has_flag(flags, "equip.weapon.sword"));

  const auto again = corundum::gameplay::item::unequip_item(flags, items, "sword");
  REQUIRE_FALSE(again.has_value());
  CHECK(again.error() == "Sword is not equipped");
}

TEST_CASE("toggle_equip: equips an unequipped item and unequips an equipped one") {
  const Registry items = make_items();
  FlagStore flags;
  flags["item.sword"] = 1;

  REQUIRE(corundum::gameplay::item::toggle_equip(flags, items, "sword").has_value());
  CHECK(corundum::gameplay::item::is_equipped(flags, items, "sword"));

  REQUIRE(corundum::gameplay::item::toggle_equip(flags, items, "sword").has_value());
  CHECK_FALSE(corundum::gameplay::item::is_equipped(flags, items, "sword"));
}
