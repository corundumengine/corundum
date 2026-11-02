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

#include <string_view>
#include <variant>
#include <vector>

namespace {

  namespace screens = corundum::gameplay::screens;
  using corundum::input::Action;
  using corundum::input::InputDevice;
  using corundum::test::RecordingRenderer;

  constexpr corundum::core::math::Vec2 k_viewport{.x = 640.f, .y = 360.f};

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
