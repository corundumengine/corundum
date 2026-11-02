// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/rules/rules.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/ui/panel_style.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace corundum::gameplay::screens {

  /** @brief One combatant's row on the combat HUD; combat::Battle refreshes the numbers every step. */
  struct CombatHudRow {
    int command_point_beats{};

    rules::Health health{};

    bool is_hostile{};

    bool is_winding_up{};

    int marker_beats{};

    std::string name{}; ///< Copied from CombatantSpec::name when the fight starts.

    int windup_end_beats{}; ///< Where the current or next command lands: the timeline bar's right end.

    std::string windup_label{}; ///< The command winding up, such as "Crushing Blow"; empty when none is.
  };

  /** @brief One command-bar slot: the action that orders it, and what it orders. */
  struct CombatHudCommand {
    input::Action action{input::Action::CombatCommand1};

    bool is_available{true}; ///< Drawn dimmed when false.

    bool is_queued{}; ///< Drawn highlighted when true.

    std::string label{};
  };

  /** @brief What the combat HUD shows: the command bar and one row per combatant. */
  struct CombatHud {
    std::vector<CombatHudCommand> commands{};

    std::vector<CombatHudRow> rows{};
  };

  /** @brief Draw the combat HUD:
   *  - a panel at the top-left with, per row, "<name>  <current> / <max>" (and "  <windup label>"
   *    while winding up), a Health bar and a timeline bar marking the command point;
   *  - the command bar centred along the bottom, each slot "<glyph> <label>" with the glyph for
   *    @p device;
   *  - and @p banner centred at the top of the viewport when it isn't empty.
   *
   *  Pure render, invoked only while Combat or CombatPaused is on top of the UI stack. */
  void combat_hud_render(platform::Renderer &r, const ui::PanelStyle &style, const ui::NinePatchBorder &border,
                         const CombatHud &hud, core::math::Vec2 viewport, input::InputDevice device,
                         std::string_view banner);

} // namespace corundum::gameplay::screens
