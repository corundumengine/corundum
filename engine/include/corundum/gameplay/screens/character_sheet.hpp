// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/rules/rules.hpp>
#include <corundum/gameplay/screens/inventory_panel.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/ui/ui_draw.hpp>
#include <corundum/world/flags.hpp>

#include <optional>
#include <string_view>
#include <vector>

namespace corundum::gameplay::screens {

/** FlagStore key holding the inventory's slot capacity; 0 means unbounded/unknown. */
inline constexpr std::string_view k_inventory_capacity_flag = "player.inventory_capacity";

/** The character sheet's display values.
 *
 *  Pure data assembled from the FlagStore and the game's ruleset by build_character_info(); the
 *  screen never reads the game itself. An equipment slot's empty item name draws as "(empty)".
 */
struct CharacterInfo {
  /** Rows the game's ruleset supplies for the Stats column, drawn above Gold in the order given;
   *  empty when the ruleset installs no character_rows callback.
   */
  std::vector<rules::StatRow> rule_rows{};

  int gold{};

  /** Distinct equipment slots among held items, as the Inventory panel's left column shows
   *  them (free-form slot names, sorted).
   */
  std::vector<EquipmentLine> equipment{};

  /** Held item rows, as the Inventory panel lists them (sorted by category then name). The
   *  character sheet highlights and equips from these; `inventory_count` mirrors its size.
   */
  std::vector<InventoryLine> inventory{};

  int inventory_count{};

  int inventory_capacity{};
};

/** Highlighted cell of the character sheet: a section column and a row within it.
 *
 *  Sections are 0 = Stats, 1 = Equipment, 2 = Inventory. Every section exposes at least one
 *  highlightable row, so the cursor always has a valid target. Equipment and Inventory rows are
 *  actionable; Stats rows only highlight.
 */
struct CharacterSheetState {
  int section{};

  int row{};
};

/** Number of highlightable rows in `section`, always at least 1.
 *
 *  Stats holds one row per ruleset row and Gold; Equipment holds one row per known slot (or the
 *  single "(empty)" row); Inventory holds one row per held item.
 *
 *  @param info    Character sheet values.
 *  @param section Section index; out-of-range values clamp to 0.
 */
[[nodiscard]] int character_section_row_count(const CharacterInfo &info, int section) noexcept;

/** Apply one navigation step to `state`.
 *
 *  `dx` moves between sections (wrapping), clamping the row into the new section; `dy` moves
 *  within the current section, wrapping past either end. Pass 0 to leave an axis untouched.
 *
 *  @param state Cursor to move.
 *  @param info  Values deciding each section's row count.
 *  @param dx    Section step, -1/0/+1.
 *  @param dy    Row step, -1/0/+1.
 */
void move_character_cursor(CharacterSheetState &state, const CharacterInfo &info, int dx, int dy) noexcept;

/** Read the character sheet's values from `flags` and `ruleset`.
 *
 *  Every field but rule_rows comes from a FlagStore key; the game decides the RPG policy and
 *  writes the keys. Missing keys take their documented defaults, and negative counts clamp to 0.
 *  `inventory_count` counts the held item rows (`item.<id>` flags with a positive count), matching
 *  the inventory panel's rows. `equipment` lists one row per equipment slot among held items,
 *  resolving the equipped item through `items`. `rule_rows` is `ruleset`'s character_rows for
 *  `flags`, or empty when that callback is empty.
 *
 *  @param flags   Active FlagStore.
 *  @param items   Loaded item registry; resolves equipment slot names and display names.
 *  @param ruleset The game's ruleset; supplies the Stats column's rule rows.
 */
[[nodiscard]] CharacterInfo build_character_info(const world::FlagStore &flags, const item::Registry &items,
                                                 const rules::Ruleset &ruleset);

/** Screen-space geometry of the character sheet, shared by render and hit-testing. */
struct CharacterSheetLayout {
  core::math::Vec2 panel_pos{};

  core::math::Vec2 panel_size{};

  ui::RowRect stats{}; ///< The ruleset's stat rows and gold column.

  ui::RowRect equipment{}; ///< Weapon/armor/accessory column.

  ui::RowRect inventory{}; ///< Item count and capacity column.
};

/** Compute the character sheet panel and section geometry for `viewport`.
 *
 *  The panel fills the viewport minus PanelStyle::margin (capped at k_screen_panel_max_width)
 *  and is split into three equal columns under a centered title. The result is a pure function
 *  of `style` and `viewport`, so render and hit-testing cannot disagree.
 *
 *  @param style    Panel style supplying fonts, sizes and spacing.
 *  @param viewport Screen size in logical pixels; the panel is centered within it.
 */
[[nodiscard]] CharacterSheetLayout character_sheet_layout(const ui::PanelStyle &style, core::math::Vec2 viewport);

/** Draw the character sheet panel: stats, equipment and inventory sections.
 *
 *  Pure render, invoked only while the Character mode is on top of the UI stack. The three
 *  sections are columns of labeled rows; the ruleset's rows sit above Gold, and the inventory
 *  capacity suffix is omitted when inventory_capacity is 0. A footer shows the Close glyph for
 *  `last_device`.
 *
 *  @param r           Renderer; receives DrawRect, nine-patch DrawSprite, and DrawText commands.
 *  @param style       Panel style supplying fonts, sizes and colours.
 *  @param border      Pre-loaded nine-patch frame; same one the other panels use.
 *  @param info        Display values to render, as build_character_info() produces.
 *  @param viewport    Screen size in logical pixels; the panel is centered within it.
 *  @param last_device Device of the player's most recent press; picks the footer glyphs.
 *  @param state       Highlighted section/row; the current section's row is drawn selected.
 */
void character_sheet_render(platform::Renderer &r, const ui::PanelStyle &style, const ui::NinePatchBorder &border,
                            const CharacterInfo &info, core::math::Vec2 viewport,
                            input::InputDevice last_device = input::InputDevice::Keyboard,
                            const CharacterSheetState &state = {});

} // namespace corundum::gameplay::screens
