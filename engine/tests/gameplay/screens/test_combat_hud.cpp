// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/screens/combat_hud.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/ui/panel_style.hpp>

#include "ui/recording_renderer.hpp"

#include <cstddef>
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

  /// The command bar's fill: after the rows panel and each of make_hud()'s two rows' five bar rects.
  constexpr std::size_t k_command_panel_rect = 11;

  /// The protagonist and a hostile winding up Crushing Blow; Shield Bash queued, the tonic spent.
  screens::CombatHud make_hud() {
    screens::CombatHud hud;
    hud.commands = {
        {.action = Action::CombatCommand1, .is_available = true, .is_queued = false, .label = "Attack"},
        {.action = Action::CombatCommand2, .is_available = true, .is_queued = true, .label = "Shield Bash"},
        {.action = Action::CombatCommand3, .is_available = false, .is_queued = false, .label = "Herbal Tonic (0)"},
    };
    hud.rows = {
        {
            .command_point_beats = 100,
            .health = {.current = 9, .max = 12},
            .is_hostile = false,
            .is_winding_up = false,
            .marker_beats = 60,
            .name = "You",
            .windup_end_beats = 120,
            .windup_label = "",
        },
        {
            .command_point_beats = 100,
            .health = {.current = 3, .max = 7},
            .is_hostile = true,
            .is_winding_up = true,
            .marker_beats = 110,
            .name = "Brann",
            .windup_end_beats = 130,
            .windup_label = "Crushing Blow",
        },
    };
    return hud;
  }

  void render(RecordingRenderer &r, InputDevice device, std::string_view banner) {
    const corundum::ui::PanelStyle style{};
    const corundum::ui::NinePatchBorder border{};
    screens::combat_hud_render(r, style, border, make_hud(), k_viewport, device, banner);
  }

  /// Render @p hud in the default style with keyboard glyphs and @p border.
  void render_hud(RecordingRenderer &r, const screens::CombatHud &hud,
                  const corundum::ui::NinePatchBorder &border = {}) {
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

TEST_CASE("combat HUD: rows show name, Health and the command winding up, and the bar names the keys") {
  RecordingRenderer r;

  render(r, InputDevice::Keyboard, "");

  CHECK_EQ(drawn_texts(r), std::vector<std::string_view>{"You  9 / 12", "Brann  3 / 7  Crushing Blow", "1 Attack",
                                                         "2 Shield Bash", "3 Herbal Tonic (0)"});
}

TEST_CASE("combat HUD: on a gamepad the command bar names its buttons") {
  RecordingRenderer r;

  render(r, InputDevice::Gamepad, "");

  CHECK_EQ(drawn_texts(r), std::vector<std::string_view>{"You  9 / 12", "Brann  3 / 7  Crushing Blow", "A Attack",
                                                         "Y Shield Bash", "B Herbal Tonic (0)"});
}

TEST_CASE("combat HUD: a banner draws last") {
  RecordingRenderer r;

  render(r, InputDevice::Keyboard, "Paused");

  CHECK_EQ(drawn_texts(r), std::vector<std::string_view>{"You  9 / 12", "Brann  3 / 7  Crushing Blow", "1 Attack",
                                                         "2 Shield Bash", "3 Herbal Tonic (0)", "Paused"});
}

TEST_CASE("combat HUD: a queued command is highlighted and an unavailable one dimmed") {
  RecordingRenderer r;
  const corundum::ui::PanelStyle style{};

  render(r, InputDevice::Keyboard, "");

  const corundum::core::math::Colour attack = colour_of(r, "1 Attack");
  const corundum::core::math::Colour queued = colour_of(r, "2 Shield Bash");
  const corundum::core::math::Colour spent = colour_of(r, "3 Herbal Tonic (0)");
  CHECK(same_colour(attack, style.body));
  CHECK(same_colour(queued, style.selected));
  CHECK_FALSE(same_colour(spent, style.body));
  CHECK_FALSE(same_colour(spent, style.selected));
}

TEST_CASE("combat HUD: short rows keep the rows panel at its minimum width") {
  RecordingRenderer r;
  screens::CombatHud hud = make_hud();
  hud.rows[1].windup_label = "";

  render_hud(r, hud);

  CHECK_EQ(drawn_rects(r)[k_rows_panel_rect].size.x, doctest::Approx(k_min_row_width + (2.f * k_thin_border_pad)));
}

TEST_CASE("combat HUD: a row longer than the minimum widens the panel and its bars") {
  RecordingRenderer r;
  screens::CombatHud hud = make_hud();
  hud.rows[1].windup_label = std::string(60, 'x');
  const float expected_row_width = k_glyph_width * static_cast<float>(std::string{"Brann  3 / 7  "}.size() + 60);

  render_hud(r, hud);

  const std::vector<corundum::platform::DrawRect> rects = drawn_rects(r);
  CHECK_EQ(rects[k_rows_panel_rect].size.x, doctest::Approx(expected_row_width + (2.f * k_thin_border_pad)));
  CHECK_EQ(rects[k_rows_panel_rect + 1].size.x, doctest::Approx(expected_row_width));
}

TEST_CASE("combat HUD: a thick border insets both panels' content past its corner cells") {
  RecordingRenderer r;
  const corundum::ui::NinePatchBorder border{.texture_id = 1, .tile_w = 32, .tile_h = 40};

  render_hud(r, make_hud(), border);

  const std::vector<corundum::platform::DrawRect> rects = drawn_rects(r);
  const corundum::platform::DrawRect rows_panel = rects[k_rows_panel_rect];
  const corundum::platform::DrawRect command_panel = rects[k_command_panel_rect];
  const corundum::platform::DrawText first_row = text_named(r, "You  9 / 12");
  const corundum::platform::DrawText first_command = text_named(r, "1 Attack");
  const corundum::platform::DrawText last_command = text_named(r, "3 Herbal Tonic (0)");
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
  const corundum::ui::NinePatchBorder border{.texture_id = 1, .tile_w = 32, .tile_h = 40};

  render_hud(r, make_hud(), border);

  const corundum::platform::DrawRect command_panel = drawn_rects(r)[k_command_panel_rect];
  CHECK_EQ(command_panel.position.y + command_panel.size.y, doctest::Approx(k_viewport.y - style.margin));
}
