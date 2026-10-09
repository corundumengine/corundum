// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/item/item.hpp>
#include <corundum/gameplay/item/registry.hpp>
#include <corundum/gameplay/screens/character_sheet.hpp>
#include <corundum/gameplay/screens/modes.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/world/flags.hpp>
#include <corundum/world/ui_stack.hpp>

#include "ui/recording_renderer.hpp"

#include <string>
#include <string_view>
#include <variant>

namespace {

  using corundum::gameplay::screens::CharacterInfo;
  using corundum::test::make_border;
  using corundum::test::RecordingRenderer;
  using corundum::world::FlagStore;

  namespace screens = corundum::gameplay::screens;

  /// A registry holding one equippable apparel item (`cloak`, slot `body`) and one weapon (`sword`).
  corundum::gameplay::item::Registry make_equipment_registry() {
    using corundum::gameplay::item::ApparelData;
    using corundum::gameplay::item::Item;
    using corundum::gameplay::item::ItemCategory;
    using corundum::gameplay::item::Registry;
    using corundum::gameplay::item::WeaponData;

    Registry items;
    items.add(Item{
        .apparel = ApparelData{.slot = "body"},
        .category = ItemCategory::Apparel,
        .id = "cloak",
        .name = "Cloak",
    });
    items.add(Item{.category = ItemCategory::Weapon, .id = "sword", .name = "Sword", .weapon = WeaponData{}});
    return items;
  }

  /// The recorded DrawText with exactly @p text, or nullptr.
  const corundum::platform::DrawText *find_text(const RecordingRenderer &r, std::string_view text) {
    for (const auto &call : r.log) {
      if (const auto *drawn = std::get_if<corundum::platform::DrawText>(&call); drawn != nullptr && drawn->text == text)
        return drawn;
    }
    return nullptr;
  }

} // namespace

TEST_CASE("character sheet: default CharacterInfo is level 1 with no progress") {
  const CharacterInfo info{};

  CHECK(info.level == 1);
  CHECK(info.experience == 0);
  CHECK(info.gold == 0);
  CHECK(info.inventory_count == 0);
  CHECK(info.inventory_capacity == 0);
  CHECK(info.equipment.empty());
}

TEST_CASE("character sheet: build_character_info reads every field from the flag store") {
  const auto items = make_equipment_registry();
  FlagStore flags;
  flags["player.level"] = 3;
  flags["player.xp"] = 130;
  flags["player.xp_next"] = 200;
  flags["player.health"] = 7;
  flags["player.max_health"] = 10;
  flags["gold"] = 42;
  flags["player.inventory_capacity"] = 8;
  flags["item.sword"] = 1;
  flags["item.cloak"] = 1;
  flags["item.potion"] = 3;
  flags["equip.body.cloak"] = 1;
  flags["not_an_item"] = 5;

  const CharacterInfo info = screens::build_character_info(flags, items);

  CHECK(info.level == 3);
  CHECK(info.experience == 130);
  CHECK(info.experience_to_next_level == 200);
  CHECK(info.health == 7);
  CHECK(info.max_health == 10);
  CHECK(info.gold == 42);
  CHECK(info.inventory_count == 3); // item rows, not total quantity (potion x3 is one row)
  CHECK(info.inventory_capacity == 8);
  REQUIRE(info.equipment.size() == 2);
  CHECK(info.equipment[0].slot == "body");
  CHECK(info.equipment[0].item_name == "Cloak");
  CHECK(info.equipment[1].slot == "weapon");
  CHECK(info.equipment[1].item_name.empty()); // sword is held but not equipped
}

TEST_CASE("character sheet: build_character_info defaults an empty store to level 1 and no progress") {
  const FlagStore flags{};
  const corundum::gameplay::item::Registry items{};
  const CharacterInfo info = screens::build_character_info(flags, items);

  CHECK(info.level == 1);
  CHECK(info.experience == 0);
  CHECK(info.health == 0);
  CHECK(info.max_health == 0);
  CHECK(info.gold == 0);
  CHECK(info.inventory_count == 0);
  CHECK(info.inventory_capacity == 0);
  CHECK(info.equipment.empty());
}

TEST_CASE("character sheet: build_character_info clamps negative counters to zero") {
  FlagStore flags;
  flags["player.xp"] = -5;
  flags["gold"] = -1;
  flags["player.inventory_capacity"] = -2;

  const corundum::gameplay::item::Registry items{};
  const CharacterInfo info = screens::build_character_info(flags, items);

  CHECK(info.experience == 0);
  CHECK(info.gold == 0);
  CHECK(info.inventory_capacity == 0);
}

TEST_CASE("character sheet: layout keeps the three sections ordered inside the panel") {
  const corundum::ui::PanelStyle style{};

  const screens::CharacterSheetLayout layout = screens::character_sheet_layout(style, {.x = 800.f, .y = 600.f});

  CHECK(layout.panel_size.x > 0.f);
  CHECK(layout.panel_size.y > 0.f);
  CHECK(layout.stats.pos.x >= layout.panel_pos.x);
  CHECK(layout.equipment.pos.x > layout.stats.pos.x);
  CHECK(layout.inventory.pos.x > layout.equipment.pos.x);
  CHECK(layout.inventory.pos.x + layout.inventory.width <= layout.panel_pos.x + layout.panel_size.x);
  CHECK(layout.stats.width == layout.equipment.width);
  CHECK(layout.stats.height == layout.inventory.height);
}

TEST_CASE("character sheet: render draws every section's labeled values") {
  RecordingRenderer r;
  const corundum::ui::PanelStyle style{};
  CharacterInfo info;
  info.level = 2;
  info.experience = 30;
  info.experience_to_next_level = 100;
  info.health = 5;
  info.max_health = 8;
  info.gold = 12;
  info.equipment = {
      {
          .slot = "body",
          .item_name = "Travel Cloak",
      },
  };
  info.inventory_count = 3;
  info.inventory_capacity = 20;

  screens::character_sheet_render(r, style, make_border(), info, {.x = 800.f, .y = 600.f});

  CHECK(find_text(r, "Character") != nullptr);
  CHECK(find_text(r, "Stats") != nullptr);
  CHECK(find_text(r, "Level: 2") != nullptr);
  CHECK(find_text(r, "Experience: 30 / 100") != nullptr);
  CHECK(find_text(r, "Health: 5 / 8") != nullptr);
  CHECK(find_text(r, "Gold: 12") != nullptr);
  CHECK(find_text(r, "body: Travel Cloak") != nullptr);
  CHECK(find_text(r, "Items: 3 / 20") != nullptr);
  CHECK(find_text(r, "Esc Close") != nullptr);
}

TEST_CASE("character sheet: render omits the health row and capacity suffix when unset") {
  RecordingRenderer r;
  const corundum::ui::PanelStyle style{};
  CharacterInfo info;
  info.experience = 30;
  info.inventory_count = 3;

  screens::character_sheet_render(r, style, make_border(), info, {.x = 800.f, .y = 600.f});

  CHECK(find_text(r, "Experience: 30") != nullptr);
  CHECK(find_text(r, "Health: 0 / 0") == nullptr);
  CHECK(find_text(r, "Slots: (empty)") != nullptr);
  CHECK(find_text(r, "Items: 3") != nullptr);
  CHECK(find_text(r, "Items: 3 / 0") == nullptr);
}

TEST_CASE("character sheet: render shows the gamepad close glyph for a gamepad press") {
  RecordingRenderer r;
  const corundum::ui::PanelStyle style{};
  const corundum::core::math::Vec2 viewport{.x = 800.f, .y = 600.f};
  const CharacterInfo info{};

  screens::character_sheet_render(r, style, make_border(), info, viewport, corundum::input::InputDevice::Gamepad);

  CHECK(find_text(r, "B Close") != nullptr);
  CHECK(find_text(r, "Esc Close") == nullptr);
}

TEST_CASE("character sheet: GameMode::Character is a distinct extension mode") {
  CHECK(screens::Character != corundum::world::GameMode::Exploring);
  CHECK(screens::Character != screens::Inventory);
  CHECK(static_cast<unsigned>(screens::Character) >= corundum::world::k_first_extension_mode);
}
