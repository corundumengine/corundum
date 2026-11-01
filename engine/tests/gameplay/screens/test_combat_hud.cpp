// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/screens/combat_hud.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/ui/panel_style.hpp>

#include "ui/recording_renderer.hpp"

#include <cstddef>
#include <string_view>
#include <variant>
#include <vector>

namespace {

  namespace screens = corundum::gameplay::screens;
  using corundum::test::RecordingRenderer;

  constexpr corundum::core::math::Vec2 k_viewport{.x = 640.f, .y = 360.f};

  std::vector<screens::CombatHudRow> make_rows() {
    return {
        {
            .command_point_beats = 100,
            .health = {.current = 9, .max = 12},
            .is_hostile = false,
            .is_winding_up = false,
            .marker_beats = 60,
            .name = "You",
            .windup_end_beats = 120,
        },
        {
            .command_point_beats = 100,
            .health = {.current = 3, .max = 7},
            .is_hostile = true,
            .is_winding_up = true,
            .marker_beats = 110,
            .name = "Brann",
            .windup_end_beats = 120,
        },
    };
  }

  std::vector<std::string_view> drawn_texts(const RecordingRenderer &r) {
    std::vector<std::string_view> texts;
    for (const auto &call : r.log) {
      if (const auto *text = std::get_if<corundum::platform::DrawText>(&call); text != nullptr)
        texts.emplace_back(text->text);
    }
    return texts;
  }

} // namespace

TEST_CASE("combat HUD: each row shows the combatant's name and Health") {
  RecordingRenderer r;
  const corundum::ui::PanelStyle style{};
  const corundum::ui::NinePatchBorder border{};

  screens::combat_hud_render(r, style, border, make_rows(), k_viewport, "");

  CHECK_EQ(drawn_texts(r), std::vector<std::string_view>{"You  9 / 12", "Brann  3 / 7"});
}

TEST_CASE("combat HUD: a banner draws after the rows") {
  RecordingRenderer r;
  const corundum::ui::PanelStyle style{};
  const corundum::ui::NinePatchBorder border{};

  screens::combat_hud_render(r, style, border, make_rows(), k_viewport, "Paused");

  CHECK_EQ(drawn_texts(r), std::vector<std::string_view>{"You  9 / 12", "Brann  3 / 7", "Paused"});
}
