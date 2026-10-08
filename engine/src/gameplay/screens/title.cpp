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
#include <span>
#include <string>
#include <string_view>

namespace corundum::gameplay::screens {

  namespace {

    constexpr std::array<TitleRow, k_title_row_count> k_title_rows{
        TitleRow::Continue, TitleRow::NewGame, TitleRow::Load, TitleRow::Settings, TitleRow::Credits, TitleRow::Quit,
    };

    // Credits is hidden when the game configures none, so Quit shifts down one index.
    constexpr std::array<TitleRow, k_title_row_count - 1> k_title_rows_without_credits{
        TitleRow::Continue, TitleRow::NewGame, TitleRow::Load, TitleRow::Settings, TitleRow::Quit,
    };

    std::string title_footer(input::InputDevice last_device) {
      return std::format("{} Select   {} Close", ui::input_glyph(input::Action::Activate, last_device),
                         ui::input_glyph(input::Action::Cancel, last_device));
    }

    /// The labels of the visible rows, packed to the front of the array; the returned span
    /// covers exactly title_visible_row_count entries.
    std::span<const std::string_view> title_labels(const TitleState &state,
                                                   std::array<std::string_view, k_title_row_count> &storage) {
      const int count = title_visible_row_count(state.credits_available);
      for (int row = 0; row < count; ++row)
        storage[static_cast<std::size_t>(row)] = title_row_label(title_row_at(row, state.credits_available));
      return {storage.data(), static_cast<std::size_t>(count)};
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
      case TitleRow::Credits:
        return "Credits";
      case TitleRow::Quit:
        return "Quit";
    }
    return "";
  }

  TitleRow title_row_at(int row, bool credits_available) noexcept {
    if (row < 0)
      return TitleRow::Continue;
    const auto index = static_cast<std::size_t>(row);
    if (credits_available)
      return index < k_title_rows.size() ? k_title_rows[index] : TitleRow::Continue;
    return index < k_title_rows_without_credits.size() ? k_title_rows_without_credits[index] : TitleRow::Continue;
  }

  bool title_row_enabled(TitleRow row, bool continue_available) noexcept {
    return row != TitleRow::Continue || continue_available;
  }

  TitleLayout title_panel_layout(const platform::Renderer &r, const ui::PanelStyle &style, const TitleState &state,
                                 std::string_view title, core::math::Vec2 viewport, input::InputDevice last_device) {
    std::array<std::string_view, k_title_row_count> storage{};
    const std::span<const std::string_view> labels = title_labels(state, storage);
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
    std::array<std::string_view, k_title_row_count> storage{};
    const std::span<const std::string_view> labels = title_labels(state, storage);
    std::array<bool, k_title_row_count> enabled{};
    const int row_count = static_cast<int>(labels.size());
    for (int row = 0; row < row_count; ++row) {
      enabled[static_cast<std::size_t>(row)] =
          title_row_enabled(title_row_at(row, state.credits_available), state.continue_available);
    }

    const detail::FramingMenuLayout framing =
        detail::framing_menu_layout(r, style, viewport, labels, title, title_footer(last_device));
    detail::framing_menu_render(r, style, border, framing, labels, std::span<const bool>(enabled).first(labels.size()),
                                state.cursor, title, title_footer(last_device), viewport);
  }

} // namespace corundum::gameplay::screens
