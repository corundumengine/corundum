// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/rules/rules.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/ui/panel_style.hpp>

#include <span>
#include <string>
#include <string_view>

namespace corundum::gameplay::screens {

  /** @brief One combatant's row on the combat HUD; combat::Battle refreshes the numbers every step. */
  struct CombatHudRow {
    int command_point_beats{};

    rules::Health health{};

    bool is_hostile{};

    bool is_winding_up{};

    int marker_beats{};

    std::string name{}; ///< Copied from CombatantSpec::name when the fight starts.

    int windup_end_beats{}; ///< The command point plus the attack's wind-up: the timeline bar's right end.
  };

  /** @brief Draw the combat HUD: a panel at the top-left with, per row, "<name>  <current> / <max>",
   *  a Health bar and a timeline bar marking the command point; and @p banner centred at the top of
   *  the viewport when it isn't empty.
   *
   *  Pure render, invoked only while Combat or CombatPaused is on top of the UI stack. */
  void combat_hud_render(platform::Renderer &r, const ui::PanelStyle &style, const ui::NinePatchBorder &border,
                         std::span<const CombatHudRow> rows, core::math::Vec2 viewport, std::string_view banner);

} // namespace corundum::gameplay::screens
