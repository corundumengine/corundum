// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/item/registry.hpp>
#include <corundum/gameplay/rules/rules.hpp>
#include <corundum/gameplay/shop/shop.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/ui/ui_draw.hpp>
#include <corundum/world/flags.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace corundum::gameplay::screens {

  /** @brief Which side of the barter screen is active. */
  enum class BarterTab : std::uint8_t {
    Buy = 0,
    Sell = 1,
  };

  /** @brief One barter row: an item, its held count (Sell only), and its unit price. */
  struct BarterLine {
    int count{};

    std::string id{};

    std::string name{};

    int unit_price{};
  };

  /** @brief Build the Buy rows: every item the shop offers, priced by the ruleset's buy_price for
   *  the player's standing with the shop's faction. A positive base price never costs less than 1
   *  gold; a base of 0 stays 0. Rows keep the shop's authored order. */
  [[nodiscard]] std::vector<BarterLine> build_barter_stock(const gameplay::shop::Shop &shop,
                                                           const gameplay::item::Registry &items,
                                                           const world::FlagStore &flags,
                                                           const rules::Ruleset &ruleset);

  /** @brief Build the Sell rows: every held item the ruleset's sell_price will buy at a price
   *  above 0, sorted by name. No sell_price callback means no rows. */
  [[nodiscard]] std::vector<BarterLine> build_barter_sell_lines(const gameplay::shop::Shop &shop,
                                                                const gameplay::item::Registry &items,
                                                                const world::FlagStore &flags,
                                                                const rules::Ruleset &ruleset);

  /** @brief Barter-screen state: active tab and the highlighted row within it. */
  struct BarterState {
    int cursor{};

    BarterTab tab{BarterTab::Buy};
  };

  /** @brief Screen-space geometry of the barter screen, shared by render and mouse hit-testing. */
  struct BarterLayout {
    core::math::Vec2 panel_pos{};

    core::math::Vec2 panel_size{};

    std::array<ui::RowRect, 2> tabs{}; ///< Buy then Sell.

    ui::ListHit rows{};
  };

  /** @brief Compute the barter panel's geometry for @p viewport. */
  [[nodiscard]] BarterLayout barter_panel_layout(const platform::Renderer &r, const ui::PanelStyle &style,
                                                 std::string_view shop_name, int gold,
                                                 const std::vector<BarterLine> &lines, const BarterState &state,
                                                 core::math::Vec2 viewport, input::InputDevice last_device);

  /** @brief Draw the barter screen: shop name, gold, and the active tab's priced rows.
   *
   *  Pure render, like inventory_panel_render. The active tab's rows are drawn with a cursor.
   *
   *  @param r           Platform renderer; receives DrawRect, nine-patch DrawSprite, and DrawText commands.
   *  @param style       Dialog text style; reused so the screen matches dialogue.
   *  @param border      Pre-loaded nine-patch frame; the same one the dialogue box uses.
   *  @param shop_name   Heading (the merchant's shop name).
   *  @param gold        The player's current gold, drawn in the header.
   *  @param lines       Rows for the active tab, as built by the build_barter_* helpers.
   *  @param state       Active tab and highlighted row.
   *  @param viewport    Screen size in pixels; the panel is centered within this.
   *  @param last_device Device of the player's most recent press; picks the footer glyphs.
   */
  void barter_panel_render(platform::Renderer &r, const ui::PanelStyle &style, const ui::NinePatchBorder &border,
                           std::string_view shop_name, int gold, const std::vector<BarterLine> &lines,
                           const BarterState &state, core::math::Vec2 viewport,
                           input::InputDevice last_device = input::InputDevice::Keyboard);

} // namespace corundum::gameplay::screens
