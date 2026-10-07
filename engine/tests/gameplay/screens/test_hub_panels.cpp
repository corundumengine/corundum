// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

// Viewport-filling hub panel geometry: Journal, Inventory and Map window their rows to the
// panel, keep the highlighted row visible, and expose hit rects that match what they draw.

#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/item/item.hpp>
#include <corundum/gameplay/item/registry.hpp>
#include <corundum/gameplay/quest/status.hpp>
#include <corundum/gameplay/screens/hub_tabs.hpp>
#include <corundum/gameplay/screens/inventory_panel.hpp>
#include <corundum/gameplay/screens/journal.hpp>
#include <corundum/gameplay/screens/map.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/ui/ui_draw.hpp>
#include <corundum/world/flags.hpp>

#include <doctest/doctest.h>

#include "ui/recording_renderer.hpp"

#include <cstddef>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

  using corundum::core::math::Vec2;
  using corundum::gameplay::screens::InventoryLayout;
  using corundum::gameplay::screens::JournalEntry;
  using corundum::gameplay::screens::JournalLayout;
  using corundum::gameplay::screens::JournalState;
  using corundum::gameplay::screens::MapEntry;
  using corundum::gameplay::screens::MapLayout;
  using corundum::test::RecordingRenderer;

  namespace screens = corundum::gameplay::screens;

  std::vector<JournalEntry> make_journal_entries(int count) {
    std::vector<JournalEntry> entries;
    entries.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
      entries.push_back(JournalEntry{
          .id = "q" + std::to_string(i),
          .name = "Quest " + std::to_string(i),
          .objective = "Objective",
          .lifecycle = corundum::gameplay::quest::Lifecycle::Active,
      });
    }
    return entries;
  }

  std::vector<MapEntry> make_map_entries(int count) {
    std::vector<MapEntry> entries;
    entries.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i)
      entries.push_back(MapEntry{.id = "l" + std::to_string(i), .name = "Location " + std::to_string(i)});
    return entries;
  }

  bool within_panel(const corundum::ui::RowRect &row, Vec2 panel_pos, Vec2 panel_size) {
    return row.pos.y >= panel_pos.y && row.pos.y + row.height <= panel_pos.y + panel_size.y + 0.5f &&
           row.pos.x >= panel_pos.x && row.pos.x + row.width <= panel_pos.x + panel_size.x + 0.5f;
  }

} // namespace

// doctest's REQUIRE/CHECK macros expand to control flow, so the assertion count — not the
// test's logic — dominates this metric.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("journal_panel_layout: fills the viewport and windows rows to keep the cursor visible") {
  RecordingRenderer r;
  const corundum::ui::PanelStyle style{};
  const std::vector<JournalEntry> entries = make_journal_entries(30);

  JournalState state{};
  state.cursor = 25;
  state.scroll = 0;
  const JournalLayout layout = screens::journal_panel_layout(r, style, entries, state, {.x = 1280.f, .y = 720.f},
                                                             corundum::input::InputDevice::Keyboard);

  // The panel spans the viewport minus margins; the tab strip inset is reserved at the top.
  CHECK(layout.panel_pos.x == style.margin);
  CHECK(layout.panel_pos.y == style.margin + screens::hub_panel_top_inset(style));
  CHECK(layout.panel_size.x == 1280.f - (style.margin * 2.f));

  REQUIRE(layout.visible_rows > 0);
  CHECK(layout.first_row > 0);                        // cursor 25 is past the first page
  CHECK(layout.first_row + layout.visible_rows > 25); // cursor stays inside the window
  CHECK(layout.rows.size() == static_cast<std::size_t>(layout.visible_rows));

  for (const corundum::ui::RowRect &row : layout.rows)
    CHECK(within_panel(row, layout.panel_pos, layout.panel_size));

  // Hit rects are the drawn rows, and a hit maps back to the absolute cursor.
  const corundum::ui::RowRect &first = layout.rows.front();
  const Vec2 centre{.x = first.pos.x + (first.width * 0.5f), .y = first.pos.y + (first.height * 0.5f)};
  CHECK(corundum::ui::hovered_row(centre, layout.rows) == 0);
  CHECK(layout.first_row <= 25);
}

TEST_CASE("journal_panel_layout: a scroll offset moves first_row") {
  RecordingRenderer r;
  const corundum::ui::PanelStyle style{};
  const std::vector<JournalEntry> entries = make_journal_entries(30);

  JournalState state{};
  state.cursor = 10;
  state.scroll = 8;
  const JournalLayout layout = screens::journal_panel_layout(r, style, entries, state, {.x = 1280.f, .y = 720.f},
                                                             corundum::input::InputDevice::Keyboard);

  CHECK(layout.first_row == 8); // cursor 10 still inside the window, so the offset holds
}

// doctest's REQUIRE/CHECK macros expand to control flow, so the assertion count — not the
// test's logic — dominates this metric.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("inventory_panel_layout: windows item rows and keeps hit rects inside the panel") {
  RecordingRenderer r;
  const corundum::ui::PanelStyle style{};
  std::vector<screens::InventoryLine> lines;
  lines.reserve(30);
  for (int i = 0; i < 30; ++i)
    lines.push_back(screens::InventoryLine{.count = 1, .name = "Item " + std::to_string(i)});

  const InventoryLayout layout = screens::inventory_panel_layout(r, style, lines, {}, 24, 0, {.x = 1280.f, .y = 720.f});

  CHECK(layout.panel_pos.y == style.margin + screens::hub_panel_top_inset(style));
  REQUIRE(layout.visible_rows > 0);
  CHECK(layout.first_row > 0);
  CHECK(layout.first_row + layout.visible_rows > 24);
  CHECK(layout.rows.size() == static_cast<std::size_t>(layout.visible_rows));
  for (const corundum::ui::RowRect &row : layout.rows)
    CHECK(within_panel(row, layout.panel_pos, layout.panel_size));

  const corundum::ui::RowRect &first = layout.rows.front();
  const Vec2 centre{.x = first.pos.x + (first.width * 0.5f), .y = first.pos.y + (first.height * 0.5f)};
  CHECK(corundum::ui::hovered_row(centre, layout.rows) == 0);
}

TEST_CASE("inventory_panel_layout: a scroll offset moves first_row") {
  RecordingRenderer r;
  const corundum::ui::PanelStyle style{};
  std::vector<screens::InventoryLine> lines;
  lines.reserve(30);
  for (int i = 0; i < 30; ++i)
    lines.push_back(screens::InventoryLine{.count = 1, .name = "Item " + std::to_string(i)});

  // Cursor 10 is inside the window starting at 8, so the offset holds.
  const InventoryLayout layout = screens::inventory_panel_layout(r, style, lines, {}, 10, 8, {.x = 1280.f, .y = 720.f});
  CHECK(layout.first_row == 8);
}

// doctest's REQUIRE/CHECK macros expand to control flow, so the assertion count — not the
// test's logic — dominates this metric.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("map_panel_layout: fills the viewport, windows rows, and scrolls") {
  RecordingRenderer r;
  const corundum::ui::PanelStyle style{};
  const std::vector<MapEntry> entries = make_map_entries(30);

  const MapLayout layout = screens::map_panel_layout(r, style, entries, /*cursor=*/26, /*scroll=*/0,
                                                     {.x = 1280.f, .y = 720.f}, corundum::input::InputDevice::Keyboard);

  CHECK(layout.panel_pos.y == style.margin + screens::hub_panel_top_inset(style));
  REQUIRE(layout.rows.visible_rows > 0);
  CHECK(layout.rows.first_row > 0);
  CHECK(layout.rows.first_row + layout.rows.visible_rows > 26);

  // Every drawn destination sits inside the panel.
  for (int i = layout.rows.first_row; i < layout.rows.first_row + layout.rows.visible_rows; ++i) {
    const float y = layout.rows.row_pos.y + (static_cast<float>(i - layout.rows.first_row) * layout.rows.row_height);
    CHECK(y >= layout.panel_pos.y);
    CHECK(y + layout.rows.row_height <= layout.panel_pos.y + layout.panel_size.y + 0.5f);
  }

  // The wheel/scroll offset moves first_row when the cursor is at the offset.
  const MapLayout scrolled =
      screens::map_panel_layout(r, style, entries, /*cursor=*/10, /*scroll=*/10, {.x = 1280.f, .y = 720.f},
                                corundum::input::InputDevice::Keyboard);
  CHECK(scrolled.rows.first_row == 10);
}

TEST_CASE("build_equipment_lines: lists held slots sorted, showing the equipped item or (empty)") {
  using corundum::gameplay::item::ApparelData;
  using corundum::gameplay::item::Item;
  using corundum::gameplay::item::ItemCategory;
  using corundum::gameplay::item::Registry;
  using corundum::gameplay::item::WeaponData;

  Registry items;
  Item cloak;
  cloak.id = "cloak";
  cloak.name = "Cloak";
  cloak.category = ItemCategory::Apparel;
  cloak.apparel = ApparelData{.slot = "body"};
  items.add(std::move(cloak));

  Item boots;
  boots.id = "boots";
  boots.name = "Boots";
  boots.category = ItemCategory::Apparel;
  boots.apparel = ApparelData{.slot = "feet"};
  items.add(std::move(boots));

  Item sword;
  sword.id = "sword";
  sword.name = "Sword";
  sword.category = ItemCategory::Weapon;
  sword.weapon = WeaponData{};
  items.add(std::move(sword));

  corundum::world::FlagStore flags;
  flags["item.cloak"] = 1;
  flags["item.boots"] = 1;
  flags["item.sword"] = 1;
  flags["equip.body.cloak"] = 1;

  std::vector<screens::EquipmentLine> lines = screens::build_equipment_lines(flags, items);
  REQUIRE(lines.size() == 3);
  CHECK(lines[0].slot == "body");
  CHECK(lines[0].item_name == "Cloak");
  CHECK(lines[1].slot == "feet");
  CHECK(lines[1].item_name.empty()); // placeholder at draw time
  CHECK(lines[2].slot == "weapon");
  CHECK(lines[2].item_name.empty());

  // Clearing the equip flag empties the slot on the next rebuild.
  corundum::world::clear_flag(flags, "equip.body.cloak");
  lines = screens::build_equipment_lines(flags, items);
  CHECK(lines[0].item_name.empty());
}

TEST_CASE("inventory_panel_render: the equipment column draws the equipped item and (empty)") {
  const corundum::ui::NinePatchBorder border = corundum::test::make_border();
  const corundum::ui::PanelStyle style{};

  const std::vector<screens::InventoryLine> lines = {
      {.count = 1, .name = "Cloak"},
  };
  const std::vector<screens::EquipmentLine> equipment = {
      {.slot = "body", .item_name = "Cloak"},
      {.slot = "feet", .item_name = ""},
  };

  RecordingRenderer r;
  screens::inventory_panel_render(r, style, border, lines, equipment, 0, 0, {.x = 1280.f, .y = 720.f});

  bool body = false;
  bool feet = false;
  for (const auto &call : r.log) {
    if (const auto *text = std::get_if<corundum::platform::DrawText>(&call); text != nullptr) {
      body = body || text->text == "body: Cloak";
      feet = feet || text->text == "feet: (empty)";
    }
  }
  CHECK(body);
  CHECK(feet);
}
