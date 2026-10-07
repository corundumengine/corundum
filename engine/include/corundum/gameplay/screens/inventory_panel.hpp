// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/item/registry.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/ui/ui_draw.hpp>
#include <corundum/world/flags.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace corundum::gameplay::screens {

  /// One row of the inventory list: the item's category, held count, and display name
  /// (falls back to the raw id).
  struct InventoryLine {
    corundum::gameplay::item::ItemCategory category = corundum::gameplay::item::ItemCategory::Misc;

    int count = 0;

    std::string name{};

    /// Flavor text shown as a tooltip under the list for the highlighted row; empty when the
    /// item has no definition or no description.
    std::string description{};

    /// Item id the row was built from, with the flag prefix stripped; useful to look up the
    /// row's definition or move it between holders.
    std::string id{};
  };

  /** @brief Collect the player's held items from the FlagStore.
   *
   * Walks @p flags, keeps every "item.<id>" key with a count > 0, strips the
   * prefix, and resolves the display name through @p items (falling back to the
   * raw id when the registry has no entry — e.g. items granted by event before
   * their definition file was added). Sorted by (category, name) so same-category
   * items cluster in a stable display order.
   */
  [[nodiscard]] std::vector<InventoryLine> build_inventory_lines(const corundum::world::FlagStore &flags,
                                                                 const corundum::gameplay::item::Registry &items);

  /** @brief Collect items held under @p flag_prefix from the FlagStore.
   *
   *  Generalization of build_inventory_lines(): every key starting with @p flag_prefix whose
   *  count is positive becomes a row, with the prefix stripped to resolve the item definition.
   *  Pass gameplay::item::k_flag_prefix for the player's inventory or
   *  gameplay::item::container_flag_prefix(id) for a container's contents.
   *
   *  @param flags       Active FlagStore.
   *  @param items       Loaded item registry; unknown ids fall back to the raw id.
   *  @param flag_prefix Key prefix to treat as held items.
   */
  [[nodiscard]] std::vector<InventoryLine> build_item_lines(const corundum::world::FlagStore &flags,
                                                            const corundum::gameplay::item::Registry &items,
                                                            std::string_view flag_prefix);

  /** @brief One equipped-slot row of the inventory's left column. */
  struct EquipmentLine {
    std::string slot{}; ///< Slot name from ApparelData::slot, or "weapon".

    std::string item_name{}; ///< Equipped item's display name; empty → "(empty)".
  };

  /** @brief Collect the player's equipment slots from the FlagStore.
   *
   *  Scans held items, keeps those with an apparel slot or a weapon payload, and for each
   *  distinct slot resolves the equipped item whose `equip.<slot>.<item id>` flag is set. Slots
   *  are sorted by name; a slot with no equipped item keeps an empty item_name. The game owns the
   *  equip flags; the framework only displays them.
   */
  [[nodiscard]] std::vector<EquipmentLine> build_equipment_lines(const corundum::world::FlagStore &flags,
                                                                 const corundum::gameplay::item::Registry &items);

  /** @brief Screen-space geometry of the inventory panel, shared by render and mouse hit-testing. */
  struct InventoryLayout {
    core::math::Vec2 panel_pos{};

    core::math::Vec2 panel_size{};

    std::vector<ui::RowRect> rows{}; ///< One hit rect per visible item row, in draw order.

    int first_row{}; ///< Absolute index of @c rows.front(); add it to a hovered index.

    int visible_rows{}; ///< Number of item rows actually drawn.
  };

  /** @brief Compute the inventory panel's geometry for @p viewport, scrolling to keep @p cursor visible.
   *
   *  @p equipment controls whether the left equipment column is reserved.
   *  @p cursor matters because the highlighted row's description forms a footer whose height
   *  shrinks the list window; pass the cursor the render will use.
   */
  [[nodiscard]] InventoryLayout inventory_panel_layout(const platform::Renderer &r, const ui::PanelStyle &style,
                                                       const std::vector<InventoryLine> &lines,
                                                       const std::vector<EquipmentLine> &equipment, int cursor,
                                                       int scroll, core::math::Vec2 viewport);

  /** @brief Draw the inventory panel filling the viewport: equipment left, held items right.
   *
   *  The list is grouped by category (consecutive rows of one category form a group headed by the
   *  category's display name; an all-Misc inventory omits headers) and scrolled so @p cursor stays
   *  visible. The left column lists the distinct equipment slots and their equipped item or
   *  "(empty)". Each item row is drawn with ui::draw_option so the highlighted row uses
   *  style.selected. An empty inventory renders a single "(empty)" line. The caller is responsible
   *  for only invoking this while the inventory mode is active.
   *
   *  @param r         Platform renderer; receives DrawRect, nine-patch DrawSprite, and DrawText commands.
   *  @param style     Dialog text style (font, size, colour); reused so the panel matches dialogue.
   *  @param border    Pre-loaded nine-patch border texture/tile dims; same one used by the dialogue box.
   *  @param lines     Inventory rows to display.
   *  @param equipment Equipment-slot rows for the left column.
   *  @param cursor    Highlighted row index into @p lines; clamped into range locally.
   *  @param scroll    Requested first visible row; clamped so @p cursor stays visible.
   *  @param viewport  Screen size in pixels; the panel fills it.
   *  @pre @p lines is sorted by (category, name), as build_inventory_lines() produces — grouping
   *       assumes rows of equal category are consecutive.
   */
  void inventory_panel_render(platform::Renderer &r, const ui::PanelStyle &style, const ui::NinePatchBorder &border,
                              const std::vector<InventoryLine> &lines, const std::vector<EquipmentLine> &equipment,
                              int cursor, int scroll, core::math::Vec2 viewport);

} // namespace corundum::gameplay::screens
