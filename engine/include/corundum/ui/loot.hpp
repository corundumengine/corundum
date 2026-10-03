// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/math/vec.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/dialog_box.hpp>
#include <corundum/ui/inventory_panel.hpp>
#include <corundum/ui/nine_patch.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace corundum::ui {

  /** @brief Which pane of the loot screen owns the cursor. */
  enum class LootPane : int {
    Container = 0,
    Player = 1,
  };

  /** @brief Loot-screen state: the active pane and the highlighted row within it. */
  struct LootState {
    int cursor{};

    LootPane pane{LootPane::Container};
  };

  /** @brief Draw the two-pane loot transfer screen: container contents left, inventory right.
   *
   *  Pure render, like inventory_panel_render. The active pane's rows are drawn with a cursor;
   *  the inactive pane's are plain. Rows are "name xcount", as build_item_lines() produces.
   *
   *  @param r              Platform renderer; receives DrawRect, nine-patch DrawSprite, and DrawText commands.
   *  @param style          Dialog text style; reused so the screen matches dialogue.
   *  @param border         Pre-loaded nine-patch frame; the same one the dialogue box uses.
   *  @param container_name Heading for the left pane (e.g. a chest or corpse name).
   *  @param container      Items inside the container.
   *  @param player         Items the player holds.
   *  @param state          Active pane and highlighted row.
   *  @param viewport       Screen size in pixels; the panel is centered within this.
   *  @param last_device    Device of the player's most recent press; picks the footer glyphs.
   */
  void loot_panel_render(platform::Renderer &r, const DialogBoxStyle &style, const NinePatchBorder &border,
                         std::string_view container_name, const std::vector<InventoryLine> &container,
                         const std::vector<InventoryLine> &player, const LootState &state, core::math::Vec2 viewport,
                         input::InputDevice last_device = input::InputDevice::Keyboard);

} // namespace corundum::ui
