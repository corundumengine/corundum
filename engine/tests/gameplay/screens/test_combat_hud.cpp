// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/screens/combat_hud.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/platform/resource_ids.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/ui/panel_style.hpp>

#include "ui/recording_renderer.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

namespace screens = corundum::gameplay::screens;
using corundum::input::Action;
using corundum::input::InputDevice;
using corundum::test::RecordingRenderer;

constexpr corundum::core::math::Vec2 k_viewport{.x = 640.f, .y = 360.f};

/// RecordingRenderer::measure_text's width for one byte of text.
constexpr float k_glyph_width = 8.f;

/// The rows panel's content width when every row is shorter than it.
constexpr float k_min_row_width = 300.f;

/// The HUD's padding when the border's cells are smaller than it.
constexpr float k_thin_border_pad = 12.f;

/// The panel fill, which panel_chrome draws before anything else in a panel.
constexpr std::size_t k_rows_panel_rect = 0;

/// The command bar's fill: after the rows panel and make_hud()'s two rows, each with a Health-bar
/// back and fill.
constexpr std::size_t k_command_panel_rect = 5;

/// The protagonist and a hostile; Shield Bash queued, the tonic spent. The gamepad shows page 1 of 2.
screens::CombatHud make_hud() {
  screens::CombatHud hud;
  hud.commands = {
      {
          .action = Action::CombatCommand1,
          .gamepad_action = Action::CombatCommand1,
          .is_available = true,
          .is_highlighted = false,
          .label = "Attack",
      },
      {
          .action = Action::CombatCommand2,
          .gamepad_action = Action::CombatCommand2,
          .is_available = true,
          .is_highlighted = true,
          .label = "Shield Bash",
      },
      {
          .action = Action::CombatCommand3,
          .gamepad_action = Action::CombatCommand3,
          .is_available = false,
          .is_highlighted = false,
          .label = "Herbal Tonic (0)",
      },
      {
          .action = Action::CombatCommand4,
          .gamepad_action = std::nullopt,
          .is_available = true,
          .is_highlighted = false,
          .label = "Salve",
      },
  };
  hud.page = {.count = 2, .index = 0};
  hud.rows = {
      {
          .health = {.current = 9, .max = 12},
          .is_hostile = false,
          .name = "You",
      },
      {
          .health = {.current = 3, .max = 7},
          .is_hostile = true,
          .name = "Brann",
      },
  };
  return hud;
}

/// make_hud() with the gamepad on page 2: Salve has A, the other commands have no button.
screens::CombatHud make_second_page_hud() {
  screens::CombatHud hud = make_hud();
  for (screens::CombatHudCommand &command : hud.commands)
    command.gamepad_action = std::nullopt;
  hud.commands[3].gamepad_action = Action::CombatCommand1;
  hud.page.index = 1;
  return hud;
}

void render(RecordingRenderer &r, const screens::CombatHud &hud, InputDevice device, std::string_view banner) {
  const corundum::ui::PanelStyle style{};
  const corundum::ui::NinePatchBorder border{};
  screens::combat_hud_render(r, style, border, hud, k_viewport, device, banner);
}

/// Render `hud` in the default style with keyboard glyphs and `border`.
void render_hud(RecordingRenderer &r, const screens::CombatHud &hud, const corundum::ui::NinePatchBorder &border = {}) {
  const corundum::ui::PanelStyle style{};
  screens::combat_hud_render(r, style, border, hud, k_viewport, InputDevice::Keyboard, "");
}

std::vector<corundum::platform::DrawRect> drawn_rects(const RecordingRenderer &r) {
  std::vector<corundum::platform::DrawRect> rects;
  for (const auto &call : r.log) {
    if (const auto *rect = std::get_if<corundum::platform::DrawRect>(&call); rect != nullptr)
      rects.push_back(*rect);
  }
  return rects;
}

corundum::platform::DrawText text_named(const RecordingRenderer &r, std::string_view text) {
  for (const auto &call : r.log) {
    if (const auto *drawn = std::get_if<corundum::platform::DrawText>(&call); drawn != nullptr && drawn->text == text)
      return *drawn;
  }
  FAIL("the HUD drew no text '" << text << "'");
  return {};
}

std::vector<std::string_view> drawn_texts(const RecordingRenderer &r) {
  std::vector<std::string_view> texts;
  for (const auto &call : r.log) {
    if (const auto *text = std::get_if<corundum::platform::DrawText>(&call); text != nullptr)
      texts.emplace_back(text->text);
  }
  return texts;
}

corundum::core::math::Colour colour_of(const RecordingRenderer &r, std::string_view text) {
  for (const auto &call : r.log) {
    if (const auto *drawn = std::get_if<corundum::platform::DrawText>(&call); drawn != nullptr && drawn->text == text)
      return drawn->colour;
  }
  FAIL("the HUD drew no text '" << text << "'");
  return {};
}

bool same_colour(corundum::core::math::Colour a, corundum::core::math::Colour b) {
  return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

} // namespace

TEST_CASE("combat HUD: rows show name and Health, and the bar names the keys") {
  RecordingRenderer r;

  render(r, make_hud(), InputDevice::Keyboard, "");

  CHECK_EQ(drawn_texts(r), std::vector<std::string_view>{"You  9 / 12", "Brann  3 / 7", "1 Attack", "2 Shield Bash",
                                                         "3 Herbal Tonic (0)", "4 Salve"});
}

TEST_CASE("combat HUD: on a gamepad the shown page names its buttons, and the bar says how to page") {
  RecordingRenderer r;

  render(r, make_hud(), InputDevice::Gamepad, "");

  CHECK_EQ(drawn_texts(r), std::vector<std::string_view>{"You  9 / 12", "Brann  3 / 7", "A Attack", "Y Shield Bash",
                                                         "B Herbal Tonic (0)", "Salve", "L1/R1 1/2"});
}

TEST_CASE("combat HUD: on a gamepad's second page the fourth command carries the button") {
  RecordingRenderer r;

  render(r, make_second_page_hud(), InputDevice::Gamepad, "");

  CHECK_EQ(drawn_texts(r), std::vector<std::string_view>{"You  9 / 12", "Brann  3 / 7", "Attack", "Shield Bash",
                                                         "Herbal Tonic (0)", "A Salve", "L1/R1 2/2"});
}

TEST_CASE("combat HUD: a banner draws last") {
  RecordingRenderer r;

  render(r, make_hud(), InputDevice::Keyboard, "Paused");

  CHECK_EQ(drawn_texts(r), std::vector<std::string_view>{"You  9 / 12", "Brann  3 / 7", "1 Attack", "2 Shield Bash",
                                                         "3 Herbal Tonic (0)", "4 Salve", "Paused"});
}

TEST_CASE("combat HUD: a queued command is highlighted, and an unavailable one dimmed") {
  RecordingRenderer r;
  const corundum::ui::PanelStyle style{};

  render(r, make_hud(), InputDevice::Keyboard, "");

  CHECK(same_colour(colour_of(r, "1 Attack"), style.body));
  CHECK(same_colour(colour_of(r, "2 Shield Bash"), style.selected));
  const corundum::core::math::Colour spent = colour_of(r, "3 Herbal Tonic (0)");
  CHECK_FALSE(same_colour(spent, style.body));
  CHECK_FALSE(same_colour(spent, style.selected));
}

TEST_CASE("combat HUD: a row longer than the minimum widens the panel and its bars") {
  RecordingRenderer r;
  screens::CombatHud hud = make_hud();
  hud.rows[1].name = std::string(60, 'x');
  const float expected_row_width = k_glyph_width * static_cast<float>(60 + std::string_view{"  3 / 7"}.size());

  render_hud(r, hud);

  const std::vector<corundum::platform::DrawRect> rects = drawn_rects(r);
  CHECK_EQ(rects[k_rows_panel_rect].size.x, doctest::Approx(expected_row_width + (2.f * k_thin_border_pad)));
  CHECK_EQ(rects[k_rows_panel_rect + 1].size.x, doctest::Approx(expected_row_width));
}

TEST_CASE("combat HUD: each row draws one bar, its Health, and no timer") {
  RecordingRenderer r;
  const corundum::ui::PanelStyle style{};

  render_hud(r, make_hud());

  // The rows panel's fill, then each row's bar back and Health fill; the command bar's fill next.
  const std::vector<corundum::platform::DrawRect> rects = drawn_rects(r);
  REQUIRE_GT(rects.size(), k_command_panel_rect);
  CHECK_EQ(rects[k_rows_panel_rect].size.x, doctest::Approx(k_min_row_width + (2.f * k_thin_border_pad)));
  CHECK_EQ(rects[1].size.x, doctest::Approx(k_min_row_width));
  CHECK_EQ(rects[2].size.x, doctest::Approx(k_min_row_width * 9.f / 12.f));
  CHECK_EQ(rects[3].size.x, doctest::Approx(k_min_row_width));
  CHECK_EQ(rects[4].size.x, doctest::Approx(k_min_row_width * 3.f / 7.f));
  CHECK_EQ(rects[k_command_panel_rect].position.y + rects[k_command_panel_rect].size.y,
           doctest::Approx(k_viewport.y - style.margin));
}

TEST_CASE("combat HUD: a thick border insets both panels' content past its corner cells") {
  RecordingRenderer r;
  const corundum::ui::NinePatchBorder border{
      .texture_id = corundum::platform::TextureId{1},
      .tile_w = 32,
      .tile_h = 40,
  };

  render_hud(r, make_hud(), border);

  const std::vector<corundum::platform::DrawRect> rects = drawn_rects(r);
  const corundum::platform::DrawRect rows_panel = rects[k_rows_panel_rect];
  const corundum::platform::DrawRect command_panel = rects[k_command_panel_rect];
  const corundum::platform::DrawText first_row = text_named(r, "You  9 / 12");
  const corundum::platform::DrawText first_command = text_named(r, "1 Attack");
  const corundum::platform::DrawText last_command = text_named(r, "4 Salve");
  const float last_command_right =
      last_command.position.x + (k_glyph_width * static_cast<float>(last_command.text.size()));
  CHECK_EQ(first_row.position.x - rows_panel.position.x, doctest::Approx(32.f));
  CHECK_EQ(first_row.position.y - rows_panel.position.y, doctest::Approx(40.f));
  CHECK_EQ(first_command.position.x - command_panel.position.x, doctest::Approx(32.f));
  CHECK_EQ(first_command.position.y - command_panel.position.y, doctest::Approx(40.f));
  CHECK_EQ(command_panel.position.x + command_panel.size.x - last_command_right, doctest::Approx(32.f));
}

TEST_CASE("combat HUD: a thick border leaves the command bar a margin from the viewport's bottom") {
  RecordingRenderer r;
  const corundum::ui::PanelStyle style{};
  const corundum::ui::NinePatchBorder border{
      .texture_id = corundum::platform::TextureId{1},
      .tile_w = 32,
      .tile_h = 40,
  };

  render_hud(r, make_hud(), border);

  const corundum::platform::DrawRect command_panel = drawn_rects(r)[k_command_panel_rect];
  CHECK_EQ(command_panel.position.y + command_panel.size.y, doctest::Approx(k_viewport.y - style.margin));
}

TEST_CASE("combat HUD: the target's row draws its name in the selected colour") {
  RecordingRenderer r;
  const corundum::ui::PanelStyle style{};
  screens::CombatHud hud = make_hud();
  hud.rows[1].is_target = true;

  render(r, hud, InputDevice::Keyboard, "");

  CHECK(same_colour(colour_of(r, "Brann  3 / 7"), style.selected));
  CHECK(same_colour(colour_of(r, "You  9 / 12"), style.body));
}

TEST_CASE("combat HUD: a hostile that isn't the target keeps its own colour") {
  RecordingRenderer r;
  const corundum::ui::PanelStyle style{};

  render(r, make_hud(), InputDevice::Keyboard, "");

  CHECK(same_colour(colour_of(r, "Brann  3 / 7"), style.speaker));
}
