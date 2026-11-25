// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/item/item.hpp>
#include <corundum/gameplay/item/registry.hpp>
#include <corundum/gameplay/rules/rules.hpp>
#include <corundum/gameplay/screens/character_sheet.hpp>
#include <corundum/gameplay/screens/modes.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/world/flags.hpp>
#include <corundum/world/ui_stack.hpp>

#include "gameplay/rules/flat_ruleset.hpp"
#include "ui/recording_renderer.hpp"

#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using corundum::gameplay::rules::Ruleset;
using corundum::gameplay::rules::StatRow;
using corundum::gameplay::screens::CharacterInfo;
using corundum::test::make_border;
using corundum::test::RecordingRenderer;
using corundum::world::FlagStore;

namespace screens = corundum::gameplay::screens;

/** A registry holding one equippable apparel item (`cloak`, slot `body`) and one weapon (`sword`). */
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

/** The recorded DrawText with exactly `text`, or nullptr. */
const corundum::platform::DrawText *find_text(const RecordingRenderer &renderer, std::string_view text) {
  for (const auto &call : renderer.log) {
    if (const auto *drawn = std::get_if<corundum::platform::DrawText>(&call); drawn != nullptr && drawn->text == text)
      return drawn;
  }
  return nullptr;
}

/** True when some recorded DrawText contains `fragment`. */
bool has_text_containing(const RecordingRenderer &renderer, std::string_view fragment) {
  for (const auto &call : renderer.log) {
    if (const auto *drawn = std::get_if<corundum::platform::DrawText>(&call);
        drawn != nullptr && drawn->text.contains(fragment))
      return true;
  }
  return false;
}

} // namespace

TEST_CASE("character sheet: build_character_info reads every field from the flag store") {
  const auto items = make_equipment_registry();
  FlagStore flags;
  flags["gold"] = 42;
  flags["player.inventory_capacity"] = 8;
  flags["item.sword"] = 1;
  flags["item.cloak"] = 1;
  flags["item.potion"] = 3;
  flags["equip.body.cloak"] = 1;
  flags["not_an_item"] = 5;

  const CharacterInfo info = screens::build_character_info(flags, items, Ruleset{});

  CHECK(info.gold == 42);
  CHECK(info.inventory_count == 3); // item rows, not total quantity (potion x3 is one row)
  CHECK(info.inventory_capacity == 8);
  REQUIRE(info.equipment.size() == 2);
  CHECK(info.equipment[0].slot == "body");
  CHECK(info.equipment[0].item_id == "cloak");
  CHECK(info.equipment[0].item_name == "Cloak");
  CHECK(info.equipment[1].slot == "weapon");
  CHECK(info.equipment[1].item_id.empty()); // sword is held but not equipped
  CHECK(info.equipment[1].item_name.empty());
  CHECK(info.inventory.size() == 3); // one row per item id, matching inventory_count
}

TEST_CASE("character sheet: build_character_info clamps negative counters to zero") {
  FlagStore flags;
  flags["gold"] = -1;
  flags["player.inventory_capacity"] = -2;

  const corundum::gameplay::item::Registry items{};
  const CharacterInfo info = screens::build_character_info(flags, items, Ruleset{});

  CHECK(info.gold == 0);
  CHECK(info.inventory_capacity == 0);
}

TEST_CASE("character sheet: build_character_info takes the ruleset's rows") {
  FlagStore flags;
  flags["test.health"] = 7;
  flags["test.max_health"] = 10;
  const corundum::gameplay::item::Registry items{};

  const CharacterInfo info = screens::build_character_info(flags, items, corundum::test::make_flat_ruleset());

  const std::vector<StatRow> expected{{.label = "Health", .value = "7 / 10"}};
  CHECK_EQ(info.rule_rows, expected);
}

TEST_CASE("character sheet: ruleset reads an absent flag as 0") {
  const FlagStore flags{};
  const corundum::gameplay::item::Registry items{};

  const CharacterInfo info = screens::build_character_info(flags, items, corundum::test::make_flat_ruleset());

  const std::vector<StatRow> expected{{.label = "Health", .value = "0 / 0"}};
  CHECK_EQ(info.rule_rows, expected);
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
  RecordingRenderer renderer;
  const corundum::ui::PanelStyle style{};
  CharacterInfo info;
  info.rule_rows = {{.label = "Level", .value = "2"}, {.label = "Health", .value = "5 / 8"}};
  info.gold = 12;
  info.equipment = {
      {
          .slot = "body",
          .item_name = "Travel Cloak",
      },
  };
  info.inventory_count = 3;
  info.inventory_capacity = 20;

  screens::character_sheet_render(renderer, style, make_border(), info, {.x = 800.f, .y = 600.f});

  CHECK(find_text(renderer, "Character") != nullptr);
  CHECK(find_text(renderer, "Stats") != nullptr);
  CHECK(find_text(renderer, "Level: 2") != nullptr);
  CHECK(find_text(renderer, "Health: 5 / 8") != nullptr);
  CHECK(find_text(renderer, "Gold: 12") != nullptr);
  CHECK(find_text(renderer, "body: Travel Cloak") != nullptr);
  CHECK(find_text(renderer, "Items: 3 / 20") != nullptr);
  CHECK(has_text_containing(renderer, "Esc Close"));
}

TEST_CASE("character sheet: render lists held items with a stack suffix") {
  RecordingRenderer renderer;
  const corundum::ui::PanelStyle style{};
  CharacterInfo info;
  info.inventory = {
      {.count = 2, .name = "Potion", .id = "potion"},
      {.count = 1, .name = "Sword", .id = "sword"},
  };

  screens::character_sheet_render(renderer, style, make_border(), info, {.x = 800.f, .y = 600.f});

  CHECK(find_text(renderer, "Potion x2") != nullptr);
  CHECK(find_text(renderer, "Sword") != nullptr);
}

TEST_CASE("character sheet: render draws no rule rows and no capacity suffix when unset") {
  RecordingRenderer renderer;
  const corundum::ui::PanelStyle style{};
  CharacterInfo info;
  info.gold = 5;
  info.inventory_count = 3;

  screens::character_sheet_render(renderer, style, make_border(), info, {.x = 800.f, .y = 600.f});

  CHECK(find_text(renderer, "Gold: 5") != nullptr);
  CHECK_FALSE(has_text_containing(renderer, "Health"));
  CHECK(find_text(renderer, "Slots: (empty)") != nullptr);
  CHECK(find_text(renderer, "Items: 3") != nullptr);
  CHECK(find_text(renderer, "Items: 3 / 0") == nullptr);
}

TEST_CASE("character sheet: render shows the gamepad close glyph for a gamepad press") {
  RecordingRenderer renderer;
  const corundum::ui::PanelStyle style{};
  const corundum::core::math::Vec2 viewport{.x = 800.f, .y = 600.f};
  const CharacterInfo info{};

  screens::character_sheet_render(renderer, style, make_border(), info, viewport,
                                  corundum::input::InputDevice::Gamepad);

  CHECK(has_text_containing(renderer, "B Close"));
  CHECK(has_text_containing(renderer, "D-Up Navigate"));
  CHECK_FALSE(has_text_containing(renderer, "Esc Close"));
}

TEST_CASE("character sheet: section row counts always expose a highlightable row") {
  CharacterInfo info;
  CHECK(screens::character_section_row_count(info, 0) == 1); // Gold
  info.rule_rows = {{.label = "Strength", .value = "+2"}, {.label = "Wisdom", .value = "+1"}};
  CHECK(screens::character_section_row_count(info, 0) == 3);
  CHECK(screens::character_section_row_count(info, 1) == 1); // empty equipment column
  CHECK(screens::character_section_row_count(info, 2) == 1); // empty inventory column
  info.equipment = {{.slot = "body"}, {.slot = "weapon"}};
  info.inventory = {{.id = "a"}, {.id = "b"}, {.id = "c"}};
  CHECK(screens::character_section_row_count(info, 1) == 2);
  CHECK(screens::character_section_row_count(info, 2) == 3);
}

TEST_CASE("character sheet: cursor moves between sections and wraps within a section") {
  CharacterInfo info;
  info.rule_rows = {{.label = "Health", .value = "7 / 10"}}; // two stats rows: Health, Gold
  screens::CharacterSheetState state{};

  screens::move_character_cursor(state, info, /*dx=*/1, /*dy=*/0);
  CHECK(state.section == 1);
  CHECK(state.row == 0);

  screens::move_character_cursor(state, info, /*dx=*/-1, /*dy=*/0);
  CHECK(state.section == 0);

  // Up from the first row wraps to the last of the new section.
  state.row = 0;
  screens::move_character_cursor(state, info, /*dx=*/0, /*dy=*/-1);
  CHECK(state.section == 0);
  CHECK(state.row == 1);

  // A section with fewer rows clamps the carried row rather than leaving it out of range.
  state = {.section = 0, .row = 1};
  screens::move_character_cursor(state, info, /*dx=*/1, /*dy=*/0);
  CHECK(state.section == 1);
  CHECK(state.row == 0);
}

TEST_CASE("character sheet: GameMode::Character is a distinct extension mode") {
  CHECK(screens::Character != corundum::world::GameMode::Exploring);
  CHECK(screens::Character != screens::Inventory);
  CHECK(static_cast<unsigned>(screens::Character) >= corundum::world::k_first_extension_mode);
}
