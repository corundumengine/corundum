// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/screens/modes.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/world/ui_stack.hpp>

#include <array>
#include <cstddef>
#include <string_view>

namespace corundum::gameplay::screens {

/** The four screens reachable from the menu hub, in tab order. */
inline constexpr std::array<world::GameMode, 4> k_hub_tab_modes{
    Inventory,
    Journal,
    Codex,
    Map,
};

/** Display label for hub tab `mode`; empty when `mode` is not a hub tab. */
[[nodiscard]] std::string_view hub_tab_label(world::GameMode mode) noexcept;

/** One tab's screen-space extent within the hub strip. */
struct HubTabRect {
  core::math::Vec2 pos{};

  float width{};
};

/** Screen-space geometry of the hub tab strip, shared by render and hit-testing.
 *
 *  The strip is centered horizontally in the top margin; each tab rect spans its label, so a
 *  click anywhere over the label selects that tab. A pure function of font metrics and viewport.
 */
struct HubTabStrip {
  float y{};

  float line_height{};

  std::array<HubTabRect, k_hub_tab_modes.size()> tabs{};
};

/** Compute hub strip geometry for `viewport`. */
[[nodiscard]] HubTabStrip hub_tab_strip(const platform::Renderer &renderer, const ui::PanelStyle &style,
                                        core::math::Vec2 viewport);

/** Height of the top gap a hub panel leaves clear for the tab strip.
 *
 *  Equals `hub_tab_strip(...).y + line_height`; pass it as the top_inset to
 *  ui::screen_panel_rect so the panel never overlaps the strip.
 */
[[nodiscard]] float hub_panel_top_inset(const ui::PanelStyle &style) noexcept;

/** Index of the tab under `cursor` in `strip`, or -1 when the cursor is outside every tab. */
[[nodiscard]] int hub_tab_at(const HubTabStrip &strip, core::math::Vec2 cursor) noexcept;

/** Draw the hub tab strip with `active` highlighted, flanked by the bumper glyphs.
 *
 *  Drawn in the top margin so it never overlaps the centered hub panels.
 *
 *  @param renderer    Platform renderer; receives DrawText commands.
 *  @param style       Dialog text style; reused so the strip matches the panels.
 *  @param active      Hub mode to highlight (one of k_hub_tab_modes).
 *  @param viewport    Screen size in pixels.
 *  @param last_device Device of the player's most recent press; picks the bumper glyphs.
 */
void hub_tab_strip_render(platform::Renderer &renderer, const ui::PanelStyle &style, world::GameMode active,
                          core::math::Vec2 viewport, input::InputDevice last_device);

} // namespace corundum::gameplay::screens
