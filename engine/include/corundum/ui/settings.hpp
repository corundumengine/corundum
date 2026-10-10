// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/math/vec.hpp>
#include <corundum/core/window_mode.hpp>
#include <corundum/input/bindings.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/ui/ui_draw.hpp>

#include <array>
#include <cstdint>
#include <string_view>

namespace corundum::ui {

  /** @brief Pages of the in-engine settings screen. */
  enum class SettingsTab : std::uint8_t {
    General,
    Controls,
  };

  /** @brief Number of SettingsTab values. */
  inline constexpr int k_settings_tab_count = 2;

  /** @brief Display label for @p tab. */
  [[nodiscard]] std::string_view settings_tab_label(SettingsTab tab) noexcept;

  /** @brief Text-reveal presets in chars-per-second multiplier; 0 means Instant (no reveal). */
  inline constexpr std::array<float, 4> k_text_speed_presets{0.f, 0.5f, 1.f, 2.f};

  /** @brief Display label for a text-speed multiplier: Instant / Slow / Normal / Fast. */
  [[nodiscard]] std::string_view text_speed_label(float speed) noexcept;

  /** @brief The next (faster) preset, wrapping to Instant past the quickest. */
  [[nodiscard]] float next_text_speed(float speed) noexcept;

  /** @brief The previous (slower) preset, wrapping to the quickest below Instant. */
  [[nodiscard]] float prev_text_speed(float speed) noexcept;

  /** @brief Master-volume change per Left/Right press, in [0, 1]. */
  inline constexpr float k_master_volume_step = 0.1f;

  /** @brief UI-scale bounds and increment. */
  inline constexpr float k_ui_scale_min = 0.75f;

  inline constexpr float k_ui_scale_max = 2.f;

  inline constexpr float k_ui_scale_step = 0.25f;

  /** @brief Rows of the General tab. */
  enum class SettingsGeneralRow : std::uint8_t {
    Volume,
    TextSpeed,
    UiScale,
    WindowMode,
    Count,
  };

  /** @brief Number of General-tab rows. */
  inline constexpr int k_settings_general_row_count = 4;

  /** @brief Display label for a General-tab row. */
  [[nodiscard]] std::string_view settings_general_row_label(SettingsGeneralRow row) noexcept;

  /** @brief Maximum list rows the Controls page shows before scrolling. */
  inline constexpr int k_settings_max_visible_rows = 8;

  /** @brief How the Settings screen is presented.
   *
   *  InGame is the compact centered panel the pause menu opens over the running world;
   *  Framing is the full-viewport page the Title opens, drawn over the opaque framing
   *  backdrop so the boot-loaded world never shows through. The push site chooses, so the
   *  same screen renders correctly in both contexts. */
  enum class SettingsPresentation : std::uint8_t {
    InGame,  ///< Compact centered panel over the world (pause menu).
    Framing, ///< Full-viewport page over an opaque backdrop (Title).
  };

  /** @brief Settings-screen state: active tab, cursor, scroll offset, presentation and rebind capture.
   *
   *  A passive value: the update path (Engine) and the render function read it, no stateful
   *  collaborators are stored. */
  struct SettingsState {
    SettingsTab tab{SettingsTab::General};

    int cursor{};

    int scroll{};

    /// Which presentation the screen draws; set by the push site, read by layout and render.
    SettingsPresentation presentation{SettingsPresentation::InGame};

    /// Controls tab: true while InputMapper is capturing the next physical press for the
    /// cursor row. Cleared by take_captured() or Cancel.
    bool rebinding{};
  };

  /** @brief The live values the Settings screen displays. Definitely not the source of truth —
   *  they are read from the engine when the screen opens and after each edit. */
  struct SettingsValues {
    float master_volume{1.f};

    float text_speed{1.f};

    float ui_scale{1.f};

    core::WindowMode window_mode{core::WindowMode::Windowed};
  };

  /** @brief The number of rows on @p tab, for cursor wrapping. */
  [[nodiscard]] int settings_row_count(SettingsTab tab) noexcept;

  /** @brief Adjust @p state.scroll so row @p state.cursor is visible within @p visible_rows. */
  void settings_scroll_to_cursor(SettingsState &state, int row_count, int visible_rows) noexcept;

  /** @brief Screen-space geometry of the settings panel, shared by render and mouse hit-testing. */
  struct SettingsLayout {
    core::math::Vec2 panel_pos{};

    core::math::Vec2 panel_size{};

    std::array<ui::RowRect, k_settings_tab_count> tabs{};

    ui::ListHit rows{};
  };

  /** @brief Compute the settings panel's geometry for @p viewport.
   *
   *  Takes the same live @p values and @p bindings as the render so the measured panel width
   *  (which depends on the longest displayed value) matches exactly. The @p state's
   *  presentation chooses between the compact centered panel (InGame) and the full-viewport
   *  framing page (Framing).
   */
  [[nodiscard]] SettingsLayout settings_panel_layout(const platform::Renderer &r, const PanelStyle &style,
                                                     const SettingsState &state, const SettingsValues &values,
                                                     const input::Bindings &bindings, core::math::Vec2 viewport,
                                                     input::InputDevice last_device);

  /** @brief Draw the settings screen: General / Controls tabs and the active page's rows.
   *
   *  Pure render. Only invoked while GameMode::Settings is on top of the UI stack. In the
   *  Framing presentation it fills the viewport over an opaque backdrop; in the InGame
   *  presentation it is the compact centered panel the pause menu opens.
   *
   *  @param r           Platform renderer.
   *  @param style       Dialog text style; reused so the screen matches dialogue.
   *  @param border      Pre-loaded nine-patch frame; the same one the dialogue box uses.
   *  @param state       Active tab, cursor, scroll, presentation and rebind flag.
   *  @param values      Live settings values shown on the General tab.
   *  @param bindings    Live binding table shown on the Controls tab.
   *  @param viewport    Screen size in pixels; the panel is centered within this.
   *  @param last_device Device of the player's most recent press; picks the footer glyphs.
   */
  void settings_panel_render(platform::Renderer &r, const PanelStyle &style, const NinePatchBorder &border,
                             const SettingsState &state, const SettingsValues &values, const input::Bindings &bindings,
                             core::math::Vec2 viewport, input::InputDevice last_device = input::InputDevice::Keyboard);

} // namespace corundum::ui
