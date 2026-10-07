// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/gameplay/screens/title.hpp>

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

    std::string title_footer(input::InputDevice last_device) {
      return std::format("{} Select   {} Close", ui::input_glyph(input::Action::Activate, last_device),
                         ui::input_glyph(input::Action::Cancel, last_device));
    }

    std::array<std::string_view, k_title_row_count> title_labels() {
      std::array<std::string_view, k_title_row_count> labels{};
      for (int row = 0; row < k_title_row_count; ++row)
        labels[static_cast<std::size_t>(row)] = title_row_label(title_row_at(row));
      return labels;
    }

  } // namespace

  std::string_view title_row_label(TitleRow row) noexcept {
    switch (row) {
      case TitleRow::Continue:
        return "Continue";
      case TitleRow::NewGame:
        return "New Game";
      case TitleRow::Load:
        return "Load";
      case TitleRow::Settings:
        return "Settings";
      case TitleRow::Quit:
        return "Quit";
    }
    return "";
  }

  TitleRow title_row_at(int row) noexcept {
    switch (row) {
      case 0:
        return TitleRow::Continue;
      case 1:
        return TitleRow::NewGame;
      case 2:
        return TitleRow::Load;
      case 3:
        return TitleRow::Settings;
      case 4:
        return TitleRow::Quit;
      default:
        return TitleRow::Continue;
    }
  }

  bool title_row_enabled(TitleRow row, bool continue_available) noexcept {
    return row != TitleRow::Continue || continue_available;
  }

  TitleLayout title_panel_layout(const platform::Renderer &r, const ui::PanelStyle &style, const TitleState & /*state*/,
                                 std::string_view title, core::math::Vec2 viewport, input::InputDevice last_device) {
    const std::array<std::string_view, k_title_row_count> labels = title_labels();
    const detail::FramingMenuLayout framing =
        detail::framing_menu_layout(r, style, viewport, labels, title, title_footer(last_device));
    return TitleLayout{
        .panel_pos = framing.panel_pos,
        .panel_size = framing.panel_size,
        .rows = framing.rows,
    };
  }

  void title_panel_render(platform::Renderer &r, const ui::PanelStyle &style, const ui::NinePatchBorder &border,
                          const TitleState &state, std::string_view title, core::math::Vec2 viewport,
                          input::InputDevice last_device) {
    const std::array<std::string_view, k_title_row_count> labels = title_labels();
    std::array<bool, k_title_row_count> enabled{};
    for (int row = 0; row < k_title_row_count; ++row)
      enabled[static_cast<std::size_t>(row)] = title_row_enabled(title_row_at(row), state.continue_available);

    const detail::FramingMenuLayout framing =
        detail::framing_menu_layout(r, style, viewport, labels, title, title_footer(last_device));
    detail::framing_menu_render(r, style, border, framing, labels, enabled, state.cursor, title,
                                title_footer(last_device), viewport);
  }

} // namespace corundum::gameplay::screens
