// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/math/vec.hpp>
#include <corundum/item/registry.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/dialog_box.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/world/flags.hpp>

#include <string>
#include <vector>

namespace corundum::ui {

  /// One row of the inventory list: the item's category, held count, and display name
  /// (falls back to the raw id).
  struct InventoryLine {
    corundum::item::ItemCategory category = corundum::item::ItemCategory::Misc;

    int count = 0;

    std::string name;
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
                                                                 const corundum::item::Registry &items);

  /** @brief Draw a centered inventory panel listing the held items, grouped by category.
   *
   * Pure render — no state, no measurement caching, like prompt_box_render. Consecutive
   * rows of one category form a group headed by the category's display name; an
   * all-Misc inventory omits headers. The panel is sized to its content; each row is
   * drawn with ui::draw_option so the highlighted row (clamped into [0, lines.size()))
   * uses style.selected. An empty inventory renders a single "(empty)" line. The caller
   * is responsible for only invoking this while the inventory mode is active.
   *
   * @param r        Platform renderer; receives DrawRect, nine-patch DrawSprite, and DrawText commands.
   * @param style    Dialog text style (font id/sizes/colours); reused so the panel matches dialogue.
   * @param border   Pre-loaded nine-patch border texture/tile dims; same one used by the dialogue box.
   * @param lines    Inventory rows to display.
   * @param cursor   Highlighted row index into @p lines; clamped into [0, lines.size()) locally.
   * @param viewport Screen size in pixels; the panel is centered within this.
   * @pre @p lines is sorted by (category, name), as build_inventory_lines() produces — grouping
   *      assumes rows of equal category are consecutive.
   */
  void inventory_panel_render(platform::Renderer &r, const DialogBoxStyle &style, const NinePatchBorder &border,
                              const std::vector<InventoryLine> &lines, int cursor, core::math::Vec2 viewport);

} // namespace corundum::ui
