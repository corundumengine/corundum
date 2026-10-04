// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/platform/renderer.hpp>
#include <corundum/ui/menu.hpp>
#include <corundum/ui/panel_style.hpp>

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

TEST_CASE("menu_command_at maps rows to commands") {
  using corundum::ui::MenuCommand;
  CHECK(corundum::ui::menu_command_at(0) == MenuCommand::Resume);
  CHECK(corundum::ui::menu_command_at(1) == MenuCommand::Settings);
  CHECK(corundum::ui::menu_command_at(2) == MenuCommand::Save);
  CHECK(corundum::ui::menu_command_at(3) == MenuCommand::Load);
  CHECK(corundum::ui::menu_command_at(4) == MenuCommand::Quit);
}

TEST_CASE("menu: renders the title, every command row, and the footer") {
  corundum::test::RecordingRenderer r;
  const corundum::ui::PanelStyle style = make_style();
  const corundum::ui::MenuState state{};

  corundum::ui::menu_panel_render(r, style, corundum::test::make_border(), state, {.x = 800.f, .y = 600.f});

  CHECK(contains_text(r, "Paused"));
  CHECK(contains_text(r, "Resume"));
  CHECK(contains_text(r, "Settings"));
  CHECK(contains_text(r, "Save"));
  CHECK(contains_text(r, "Load"));
  CHECK(contains_text(r, "Quit"));
  CHECK(contains_text(r, "Enter Select   Esc Close"));
}
