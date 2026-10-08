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

  /** @brief Title-screen rows, in draw order. Continue is first so it can start unselected when
   *  there is nothing to continue. */
  enum class TitleRow : std::uint8_t {
    Continue,
    NewGame,
    Load,
    Settings,
    Credits,
    Quit,
  };

  /** @brief Number of title rows when every row is visible (Credits included). */
  inline constexpr int k_title_row_count = 6;

  /** @brief Number of rows drawn for a title with @p credits_available.
   *
   *  The Credits row is hidden, not disabled, when the game has no credits: a dead row would
   *  advertise content the player cannot reach. */
  [[nodiscard]] constexpr int title_visible_row_count(bool credits_available) noexcept {
    return credits_available ? k_title_row_count : k_title_row_count - 1;
  }

  /** @brief Display label for @p row. */
  [[nodiscard]] std::string_view title_row_label(TitleRow row) noexcept;

  /** @brief The visible row at @p row.
   *  @pre 0 <= @p row < title_visible_row_count(@p credits_available). */
  [[nodiscard]] TitleRow title_row_at(int row, bool credits_available) noexcept;

  /** @brief True when @p row can be activated.
   *
   *  Only Continue is conditional: with no valid save it is disabled, and the cursor starts on
   *  New Game instead. */
  [[nodiscard]] bool title_row_enabled(TitleRow row, bool continue_available) noexcept;

  /** @brief Title-screen state: the highlighted row and whether Continue and Credits are offered. */
  struct TitleState {
    int cursor{};

    bool continue_available{};

    bool credits_available{};
  };

  /** @brief Screen-space geometry of the title menu, shared by render and mouse hit-testing. */
  struct TitleLayout {
    core::math::Vec2 panel_pos{};

    core::math::Vec2 panel_size{};

    ui::ListHit rows{};
  };

  /** @brief Compute the title menu's panel and row geometry for @p viewport.
   *
   *  @param title   Game name drawn above the rows; its width is measured here so the layout
   *                 matches title_panel_render exactly. The game supplies it (GameConfig::title,
   *                 falling back to window_title), so the framework carries no branding of its own.
   */
  [[nodiscard]] TitleLayout title_panel_layout(const platform::Renderer &r, const ui::PanelStyle &style,
                                               const TitleState &state, std::string_view title,
                                               core::math::Vec2 viewport, input::InputDevice last_device);

  /** @brief Draw the title screen over an opaque backdrop.
   *
   *  Pure render, invoked only while Title is on top of the UI stack. Disabled rows (Continue
   *  with no save) draw dimmed and cannot be selected.
   *
   *  @param r            Renderer; emits a full-viewport backdrop, the panel chrome and text.
   *  @param style        Panel style supplying fonts, sizes and colours.
   *  @param border       Pre-loaded nine-patch frame.
   *  @param state        Highlighted row, Continue availability and credits visibility.
   *  @param title        Game name drawn above the rows.
   *  @param viewport     Screen size in logical pixels.
   *  @param last_device  Device of the player's most recent press; picks the footer glyphs.
   */
  void title_panel_render(platform::Renderer &r, const ui::PanelStyle &style, const ui::NinePatchBorder &border,
                          const TitleState &state, std::string_view title, core::math::Vec2 viewport,
                          input::InputDevice last_device = input::InputDevice::Keyboard);

} // namespace corundum::gameplay::screens
