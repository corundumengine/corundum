// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/screens/inventory_panel.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/ui/ui_draw.hpp>
#include <corundum/world/flags.hpp>

#include <string_view>
#include <vector>

namespace corundum::gameplay::screens {

  /** @brief FlagStore key holding the player's accumulated experience. `grant_xp(n)` adds @c n. */
  inline constexpr std::string_view k_experience_flag = "player.xp";

  /** @brief FlagStore key holding the player's character level; defaults to 1 when absent. */
  inline constexpr std::string_view k_level_flag = "player.level";

  /** @brief FlagStore key holding the experience total for the next level; 0 means unknown. */
  inline constexpr std::string_view k_next_level_experience_flag = "player.xp_next";

  /** @brief FlagStore key holding the player's current health; 0 hides the health row. */
  inline constexpr std::string_view k_health_flag = "player.health";

  /** @brief FlagStore key holding the player's maximum health; 0 hides the health row. */
  inline constexpr std::string_view k_max_health_flag = "player.max_health";

  /** @brief FlagStore key holding the inventory's slot capacity; 0 means unbounded/unknown. */
  inline constexpr std::string_view k_inventory_capacity_flag = "player.inventory_capacity";

  /** @brief The character sheet's display values.
   *
   *  Pure data assembled from the FlagStore by build_character_info(); the screen never reads
   *  the game itself. An equipment slot's empty item name draws as "(empty)".
   */
  struct CharacterInfo {
    int level{1};

    int experience{};

    int experience_to_next_level{};

    int health{};

    int max_health{};

    int gold{};

    /** Distinct equipment slots among held items, as the Inventory panel's left column shows
     *  them (free-form slot names, sorted). */
    std::vector<EquipmentLine> equipment{};

    /** Held item rows, as the Inventory panel lists them (sorted by category then name). The
     *  character sheet highlights and equips from these; @c inventory_count mirrors its size. */
    std::vector<InventoryLine> inventory{};

    int inventory_count{};

    int inventory_capacity{};
  };

  /** @brief Highlighted cell of the character sheet: a section column and a row within it.
   *
   *  Sections are 0 = Stats, 1 = Equipment, 2 = Inventory. Every section exposes at least one
   *  highlightable row, so the cursor always has a valid target; only Equipment and Inventory
   *  rows are actionable (Stats rows highlight but do nothing on activate).
   */
  struct CharacterSheetState {
    int section{};

    int row{};
  };

  /** @brief Number of highlightable rows in @p section, always at least 1.
   *
   *  Stats holds Level, Experience, an optional Health row, and Gold; Equipment holds one row
   *  per known slot (or the single "(empty)" row); Inventory holds one row per held item.
   *
   *  @param info    Character sheet values.
   *  @param section Section index; out-of-range values clamp to 0.
   */
  [[nodiscard]] int character_section_row_count(const CharacterInfo &info, int section) noexcept;

  /** @brief Apply one navigation step to @p state.
   *
   *  @p dx moves between sections (wrapping), clamping the row into the new section; @p dy moves
   *  within the current section, wrapping past either end. Pass 0 to leave an axis untouched.
   *
   *  @param[in,out] state Cursor to move.
   *  @param[in]     info  Values deciding each section's row count.
   *  @param[in]     dx    Section step, -1/0/+1.
   *  @param[in]     dy    Row step, -1/0/+1.
   */
  void move_character_cursor(CharacterSheetState &state, const CharacterInfo &info, int dx, int dy) noexcept;

  /** @brief Read the character sheet's values from @p flags.
   *
   *  Every field comes from a FlagStore key; the game decides the RPG policy (how much XP a
   *  quest grants, when a level is reached) and writes the keys. Missing keys take their
   *  documented defaults, and negative counts clamp to 0. `inventory_count` counts the held
   *  item rows (`item.<id>` flags with a positive count), matching the inventory panel's rows.
   *  `equipment` lists one row per equipment slot among held items, resolving the equipped item through @p items.
   *
   *  @param flags Active FlagStore.
   *  @param items Loaded item registry; resolves equipment slot names and display names.
   */
  [[nodiscard]] CharacterInfo build_character_info(const world::FlagStore &flags, const item::Registry &items);

  /** @brief Screen-space geometry of the character sheet, shared by render and hit-testing. */
  struct CharacterSheetLayout {
    core::math::Vec2 panel_pos{};

    core::math::Vec2 panel_size{};

    ui::RowRect stats{}; ///< Level/experience/health/gold column.

    ui::RowRect equipment{}; ///< Weapon/armor/accessory column.

    ui::RowRect inventory{}; ///< Item count and capacity column.
  };

  /** @brief Compute the character sheet panel and section geometry for @p viewport.
   *
   *  The panel fills the viewport minus PanelStyle::margin (capped at k_screen_panel_max_width)
   *  and is split into three equal columns under a centered title. The result is a pure function
   *  of @p style and @p viewport, so render and hit-testing cannot disagree.
   *
   *  @param style    Panel style supplying fonts, sizes and spacing.
   *  @param viewport Screen size in logical pixels; the panel is centered within it.
   */
  [[nodiscard]] CharacterSheetLayout character_sheet_layout(const ui::PanelStyle &style, core::math::Vec2 viewport);

  /** @brief Draw the character sheet panel: stats, equipment and inventory sections.
   *
   *  Pure render, invoked only while the Character mode is on top of the UI stack. The three
   *  sections are columns of labeled rows; the health row is omitted when max_health is 0, and
   *  the inventory capacity suffix when inventory_capacity is 0. A footer shows the Close glyph
   *  for @p last_device.
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
