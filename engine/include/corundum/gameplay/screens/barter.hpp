// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/item/registry.hpp>
#include <corundum/gameplay/shop/shop.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/ui/ui_draw.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace corundum::gameplay::screens {

/** One barter row: an item and its unit price. */
struct BarterLine {
  std::string id{};

  std::string name{};

  int unit_price{};
};

/** Build the barter rows: every item the shop offers, at its base price (the stock entry's
 *  price, or else the item's). Rows keep the shop's authored order.
 */
[[nodiscard]] std::vector<BarterLine> build_barter_stock(const gameplay::shop::Shop &shop,
                                                         const gameplay::item::Registry &items);

/** Barter-screen state: the highlighted row. */
struct BarterState {
  int cursor{};
};

/** Screen-space geometry of the barter screen, shared by render and mouse hit-testing. */
struct BarterLayout {
  core::math::Vec2 panel_pos{};

  core::math::Vec2 panel_size{};

  ui::ListHit rows{};
};

/** Compute the barter panel's geometry for `viewport`. */
[[nodiscard]] BarterLayout barter_panel_layout(const platform::Renderer &r, const ui::PanelStyle &style,
                                               std::string_view shop_name, int gold,
                                               const std::vector<BarterLine> &lines, core::math::Vec2 viewport,
                                               input::InputDevice last_device);

/** Draw the barter screen: shop name, gold, and the shop's priced rows.
 *
 *  Pure render, like inventory_panel_render. The rows are drawn with a cursor.
 *
 *  @param r           Platform renderer; receives DrawRect, nine-patch DrawSprite, and DrawText commands.
 *  @param style       Dialog text style; reused so the screen matches dialogue.
 *  @param border      Pre-loaded nine-patch frame; the same one the dialogue box uses.
 *  @param shop_name   Heading (the merchant's shop name).
 *  @param gold        The player's current gold, drawn in the header.
 *  @param lines       Rows, as build_barter_stock builds them.
 *  @param state       The highlighted row.
 *  @param viewport    Screen size in pixels; the panel is centered within this.
 *  @param last_device Device of the player's most recent press; picks the footer glyphs.
 */
void barter_panel_render(platform::Renderer &r, const ui::PanelStyle &style, const ui::NinePatchBorder &border,
                         std::string_view shop_name, int gold, const std::vector<BarterLine> &lines,
                         const BarterState &state, core::math::Vec2 viewport,
                         input::InputDevice last_device = input::InputDevice::Keyboard);

} // namespace corundum::gameplay::screens
