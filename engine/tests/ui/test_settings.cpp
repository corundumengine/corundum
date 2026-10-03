// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/core/window_mode.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/bindings.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/ui/settings.hpp>

#include "ui/recording_renderer.hpp"

#include <string_view>
#include <variant>

namespace {

  bool contains_text(const corundum::test::RecordingRenderer &r, std::string_view needle) {
    for (const auto &call : r.log) {
      if (const auto *text = std::get_if<corundum::platform::DrawText>(&call); text != nullptr && text->text == needle)
        return true;
    }
    return false;
  }

  corundum::ui::PanelStyle make_style() {
    corundum::ui::PanelStyle style{};
    style.font_id = 2;
    return style;
  }

} // namespace

TEST_CASE("text speed presets cycle and label") {
  using corundum::ui::k_text_speed_presets;
  CHECK(corundum::ui::text_speed_label(k_text_speed_presets[0]) == "Instant");
  CHECK(corundum::ui::text_speed_label(k_text_speed_presets[1]) == "Slow");
  CHECK(corundum::ui::text_speed_label(k_text_speed_presets[2]) == "Normal");
  CHECK(corundum::ui::text_speed_label(k_text_speed_presets[3]) == "Fast");

  CHECK(corundum::ui::next_text_speed(1.f) == 2.f);
  CHECK(corundum::ui::prev_text_speed(1.f) == 0.5f);
  CHECK(corundum::ui::next_text_speed(2.f) == 0.f); // wraps to Instant
  CHECK(corundum::ui::prev_text_speed(0.f) == 2.f); // wraps to Fast
}

TEST_CASE("settings row counts per tab") {
  CHECK(corundum::ui::settings_row_count(corundum::ui::SettingsTab::General) ==
        corundum::ui::k_settings_general_row_count);
  CHECK(corundum::ui::settings_row_count(corundum::ui::SettingsTab::Controls) ==
        static_cast<int>(corundum::input::k_action_count));
}

TEST_CASE("settings_scroll_to_cursor follows the cursor past the visible window") {
  corundum::ui::SettingsState state{};

  state.cursor = 0;
  corundum::ui::settings_scroll_to_cursor(state, 20, 8);
  CHECK(state.scroll == 0);

  state.cursor = 9;
  corundum::ui::settings_scroll_to_cursor(state, 20, 8);
  CHECK(state.scroll == 2);

  state.cursor = 0;
  corundum::ui::settings_scroll_to_cursor(state, 20, 8);
  CHECK(state.scroll == 0);

  // A list shorter than the window never scrolls.
  state.cursor = 3;
  corundum::ui::settings_scroll_to_cursor(state, 4, 8);
  CHECK(state.scroll == 0);
}

TEST_CASE("settings: General tab renders the row labels and their values") {
  corundum::test::RecordingRenderer r;
  const corundum::ui::PanelStyle style = make_style();
  const corundum::ui::SettingsState state{};
  const corundum::ui::SettingsValues values{
      .master_volume = 0.5f,
      .text_speed = 0.f,
      .ui_scale = 1.25f,
      .window_mode = corundum::core::WindowMode::Fullscreen,
  };

  corundum::ui::settings_panel_render(r, style, corundum::test::make_border(), state, values,
                                      corundum::input::default_bindings(), {.x = 800.f, .y = 600.f});

  CHECK(contains_text(r, "Settings"));
  CHECK(contains_text(r, "General"));
  CHECK(contains_text(r, "Controls"));
  CHECK(contains_text(r, "Master Volume"));
  CHECK(contains_text(r, "50%"));
  CHECK(contains_text(r, "Text Speed"));
  CHECK(contains_text(r, "Instant"));
  CHECK(contains_text(r, "UI Scale"));
  CHECK(contains_text(r, "125%"));
  CHECK(contains_text(r, "Window Mode"));
  CHECK(contains_text(r, "fullscreen"));
}

TEST_CASE("settings: Controls tab renders action names and their bound inputs") {
  corundum::test::RecordingRenderer r;
  const corundum::ui::PanelStyle style = make_style();
  corundum::ui::SettingsState state{};
  state.tab = corundum::ui::SettingsTab::Controls;
  state.cursor = static_cast<int>(corundum::input::Action::MoveUp);
  const corundum::ui::SettingsValues values{};

  corundum::ui::settings_panel_render(r, style, corundum::test::make_border(), state, values,
                                      corundum::input::default_bindings(), {.x = 800.f, .y = 600.f});

  CHECK(contains_text(r, "MoveUp"));
  CHECK(contains_text(r, "W, Up, LeftStickUp, DpadUp"));
}

TEST_CASE("settings: the row being rebound shows the capture prompt") {
  corundum::test::RecordingRenderer r;
  const corundum::ui::PanelStyle style = make_style();
  corundum::ui::SettingsState state{};
  state.tab = corundum::ui::SettingsTab::Controls;
  state.cursor = static_cast<int>(corundum::input::Action::MoveUp);
  state.rebinding = true;
  const corundum::ui::SettingsValues values{};

  corundum::ui::settings_panel_render(r, style, corundum::test::make_border(), state, values,
                                      corundum::input::default_bindings(), {.x = 800.f, .y = 600.f});

  CHECK(contains_text(r, "(press a key)"));
}
