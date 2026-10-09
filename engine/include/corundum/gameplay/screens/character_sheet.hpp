// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/math/vec.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/ui/ui_draw.hpp>
#include <corundum/world/flags.hpp>

#include <string>
#include <string_view>

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

  /** @brief The three equipped item names a character sheet shows.
   *
   *  A display model, not the inventory's own slot list: the game resolves each name from
   *  whichever equip flag backs it, so the ship-time equipment system can change without
   *  changing this screen.
   */
  struct CharacterEquipment {
    std::string weapon{};

    std::string armor{};

    std::string accessory{};
  };

  /** @brief The character sheet's display values.
   *
   *  Pure data assembled from the FlagStore by build_character_info(); the screen never reads
   *  the game itself. An empty equipment name draws as "(empty)".
   */
  struct CharacterInfo {
    int level{1};

    int experience{};

    int experience_to_next_level{};

    int health{};

    int max_health{};

    int gold{};

    CharacterEquipment equipment{};

    int inventory_count{};

    int inventory_capacity{};
  };

  /** @brief Read the character sheet's values from @p flags.
   *
   *  Every field comes from a FlagStore key; the game decides the RPG policy (how much XP a
   *  quest grants, when a level is reached) and writes the keys. Missing keys take their
   *  documented defaults, and negative counts clamp to 0. `inventory_count` counts the held
   *  item rows (`item.<id>` flags with a positive count), matching the inventory panel's rows.
   *
   *  @param flags Active FlagStore.
   */
  [[nodiscard]] CharacterInfo build_character_info(const world::FlagStore &flags);

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
   */
  void character_sheet_render(platform::Renderer &r, const ui::PanelStyle &style, const ui::NinePatchBorder &border,
                              const CharacterInfo &info, core::math::Vec2 viewport,
                              input::InputDevice last_device = input::InputDevice::Keyboard);

} // namespace corundum::gameplay::screens
