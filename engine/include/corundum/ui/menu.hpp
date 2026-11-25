// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/math/vec.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/ui/ui_draw.hpp>

#include <cstdint>
#include <string_view>

namespace corundum::ui {

/** What the player picked from the pause menu. */
enum class MenuCommand : std::uint8_t {
  Resume,
  Settings,
  Save,
  Load,
  Quit,
  Count,
};

/** Number of pause-menu rows. */
inline constexpr int k_menu_command_count = static_cast<int>(MenuCommand::Count);

/** Display label for `command`. */
[[nodiscard]] std::string_view menu_command_label(MenuCommand command) noexcept;

/** The command at row `row`.
 *  @pre 0 <= `row` < k_menu_command_count.
 */
[[nodiscard]] MenuCommand menu_command_at(int row) noexcept;

/** Pause-menu selection state: the highlighted row only. */
struct MenuState {
  int cursor{};
};

/** Screen-space geometry of the pause menu, shared by render and mouse hit-testing. */
struct MenuLayout {
  core::math::Vec2 panel_pos{};

  core::math::Vec2 panel_size{};

  ListHit rows{};
};

/** Compute the pause menu's panel and row geometry for `viewport`. */
[[nodiscard]] MenuLayout menu_panel_layout(const platform::Renderer &renderer, const PanelStyle &style,
                                           core::math::Vec2 viewport, input::InputDevice last_device);

/** Draw the centered pause menu with Resume / Settings / Save / Load / Quit rows.
 *
 *  Pure render, like inventory_panel_render. Only invoked while GameMode::Menu is on top of
 *  the UI stack.
 *
 *  @param renderer    Platform renderer; receives DrawRect, nine-patch DrawSprite and DrawText commands.
 *  @param style       Dialog text style; reused so the menu matches dialogue.
 *  @param border      Pre-loaded nine-patch frame; the same one the dialogue box uses.
 *  @param state       Highlighted row.
 *  @param viewport    Screen size in pixels; the panel is centered within this.
 *  @param last_device Device of the player's most recent press; picks the footer glyphs.
 */
void menu_panel_render(platform::Renderer &renderer, const PanelStyle &style, const NinePatchBorder &border,
                       const MenuState &state, core::math::Vec2 viewport,
                       input::InputDevice last_device = input::InputDevice::Keyboard);

} // namespace corundum::ui
