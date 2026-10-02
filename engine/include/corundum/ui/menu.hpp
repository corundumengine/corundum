// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/math/vec.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/dialog_box.hpp>
#include <corundum/ui/nine_patch.hpp>

#include <cstdint>
#include <string_view>

namespace corundum::ui {

  /** @brief What the player picked from the pause menu. */
  enum class MenuCommand : std::uint8_t {
    Resume,
    Settings,
    Quit,
  };

  /** @brief Number of pause-menu rows. */
  inline constexpr int k_menu_command_count = 3;

  /** @brief Display label for @p command. */
  [[nodiscard]] std::string_view menu_command_label(MenuCommand command) noexcept;

  /** @brief The command at row @p row.
   *  @pre 0 <= @p row < k_menu_command_count. */
  [[nodiscard]] MenuCommand menu_command_at(int row) noexcept;

  /** @brief Pause-menu selection state: the highlighted row only. */
  struct MenuState {
    int cursor{};
  };

  /** @brief Draw the centered pause menu with Resume / Settings / Quit rows.
   *
   *  Pure render, like inventory_panel_render. Only invoked while GameMode::Menu is on top of
   *  the UI stack.
   *
   *  @param r           Platform renderer; receives DrawRect, nine-patch DrawSprite and DrawText commands.
   *  @param style       Dialog text style; reused so the menu matches dialogue.
   *  @param border      Pre-loaded nine-patch frame; the same one the dialogue box uses.
   *  @param state       Highlighted row.
   *  @param viewport    Screen size in pixels; the panel is centered within this.
   *  @param last_device Device of the player's most recent press; picks the footer glyphs.
   */
  void menu_panel_render(platform::Renderer &r, const DialogBoxStyle &style, const NinePatchBorder &border,
                         const MenuState &state, core::math::Vec2 viewport,
                         input::InputDevice last_device = input::InputDevice::Keyboard);

} // namespace corundum::ui
