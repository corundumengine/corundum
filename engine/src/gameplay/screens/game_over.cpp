// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/gameplay/screens/game_over.hpp>

#include "gameplay/screens/framing_menu.hpp"

#include <corundum/core/math/vec.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/input_glyph.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/ui/panel_style.hpp>

#include <array>
#include <cstddef>
#include <format>
#include <string>
#include <string_view>

namespace corundum::gameplay::screens {

  namespace {

    constexpr std::string_view k_game_over_heading = "Game Over";

    std::string game_over_footer(input::InputDevice last_device) {
      return std::format("{} Select   {} Close", ui::input_glyph(input::Action::Activate, last_device),
                         ui::input_glyph(input::Action::Cancel, last_device));
    }

    std::array<std::string_view, k_game_over_row_count> game_over_labels() {
      std::array<std::string_view, k_game_over_row_count> labels{};
      for (int row = 0; row < k_game_over_row_count; ++row)
        labels[static_cast<std::size_t>(row)] = game_over_row_label(game_over_row_at(row));
      return labels;
    }

  } // namespace

  std::string_view game_over_row_label(GameOverRow row) noexcept {
    switch (row) {
      case GameOverRow::Reload:
        return "Reload";
      case GameOverRow::Load:
        return "Load";
      case GameOverRow::ReturnToTitle:
        return "Return to Title";
    }
    return "";
  }

  GameOverRow game_over_row_at(int row) noexcept {
    switch (row) {
      case 0:
        return GameOverRow::Reload;
      case 1:
        return GameOverRow::Load;
      case 2:
        return GameOverRow::ReturnToTitle;
      default:
        return GameOverRow::Reload;
    }
  }

  GameOverLayout game_over_panel_layout(const platform::Renderer &r, const ui::PanelStyle &style,
                                        core::math::Vec2 viewport, input::InputDevice last_device) {
    const std::array<std::string_view, k_game_over_row_count> labels = game_over_labels();
    const detail::FramingMenuLayout framing =
        detail::framing_menu_layout(r, style, viewport, labels, k_game_over_heading, game_over_footer(last_device));
    return GameOverLayout{
        .panel_pos = framing.panel_pos,
        .panel_size = framing.panel_size,
        .rows = framing.rows,
    };
  }

  void game_over_panel_render(platform::Renderer &r, const ui::PanelStyle &style, const ui::NinePatchBorder &border,
                              const GameOverState &state, core::math::Vec2 viewport, input::InputDevice last_device) {
    const std::array<std::string_view, k_game_over_row_count> labels = game_over_labels();
    std::array<bool, k_game_over_row_count> enabled{};
    for (int row = 0; row < k_game_over_row_count; ++row) {
      const GameOverRow command = game_over_row_at(row);
      enabled[static_cast<std::size_t>(row)] = command != GameOverRow::Reload || state.reload_available;
    }

    const detail::FramingMenuLayout framing =
        detail::framing_menu_layout(r, style, viewport, labels, k_game_over_heading, game_over_footer(last_device));
    detail::framing_menu_render(r, style, border, framing, labels, enabled, state.cursor, k_game_over_heading,
                                game_over_footer(last_device), viewport);
  }

} // namespace corundum::gameplay::screens
