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

namespace corundum::gameplay::screens {

  /** @brief Game-over rows, in draw order. Reload is first so it can be disabled when the
   *  session has never saved or loaded. */
  enum class GameOverRow : std::uint8_t {
    Reload,
    Load,
    ReturnToTitle,
  };

  /** @brief Number of game-over rows. */
  inline constexpr int k_game_over_row_count = 3;

  /** @brief Display label for @p row. */
  [[nodiscard]] std::string_view game_over_row_label(GameOverRow row) noexcept;

  /** @brief The row at @p row.
   *  @pre 0 <= @p row < k_game_over_row_count. */
  [[nodiscard]] GameOverRow game_over_row_at(int row) noexcept;

  /** @brief Game-over state: the highlighted row and whether Reload is selectable. */
  struct GameOverState {
    int cursor{};

    bool reload_available{};
  };

  /** @brief Screen-space geometry of the game-over menu, shared by render and mouse hit-testing. */
  struct GameOverLayout {
    core::math::Vec2 panel_pos{};

    core::math::Vec2 panel_size{};

    ui::ListHit rows{};
  };

  /** @brief Compute the game-over menu's panel and row geometry for @p viewport. */
  [[nodiscard]] GameOverLayout game_over_panel_layout(const platform::Renderer &r, const ui::PanelStyle &style,
                                                      core::math::Vec2 viewport, input::InputDevice last_device);

  /** @brief Draw the game-over screen over an opaque backdrop.
   *
   *  Pure render, invoked only while GameOver is on top of the UI stack. Reload draws dimmed
   *  when the session has no last slot.
   *
   *  @param r           Renderer; emits a full-viewport backdrop, panel chrome and text.
   *  @param style       Panel style supplying fonts, sizes and colours.
   *  @param border      Pre-loaded nine-patch frame.
   *  @param state       Highlighted row and Reload availability.
   *  @param viewport    Screen size in logical pixels.
   *  @param last_device Device of the player's most recent press; picks the footer glyphs.
   */
  void game_over_panel_render(platform::Renderer &r, const ui::PanelStyle &style, const ui::NinePatchBorder &border,
                              const GameOverState &state, core::math::Vec2 viewport,
                              input::InputDevice last_device = input::InputDevice::Keyboard);

} // namespace corundum::gameplay::screens
