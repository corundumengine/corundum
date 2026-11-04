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

#include <optional>
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

    std::vector<std::string> status_labels{}; ///< Drawn under the name joined by ", ", such as "Momentum 2, Weakened".

    int windup_end_beats{}; ///< Where the current or next command lands: the timeline bar's right end.

    std::string windup_label{}; ///< The command winding up, such as "Crushing Blow"; empty when none is.
  };

  /** @brief One command-bar slot: the actions that press it, and what it does. */
  struct CombatHudCommand {
    input::Action action{input::Action::CombatCommand1}; ///< The key that presses it.

    /** The action whose gamepad button presses it while its page shows; nullopt off the page. */
    std::optional<input::Action> gamepad_action{};

    bool is_available{true}; ///< Drawn dimmed when false.

    bool is_highlighted{}; ///< Drawn highlighted: a queued command, or a toggle that is on.

    std::string label{};
  };

  /** @brief Which three slots a gamepad's A, Y and B press: slots 3 × index to 3 × index + 2. */
  struct CombatHudPage {
    int count{1};

    int index{}; ///< Below count.
  };

  /** @brief What the combat HUD shows: the command bar and one row per combatant. */
  struct CombatHud {
    std::vector<CombatHudCommand> commands{}; ///< Attack, then the protagonist's commands.

    CombatHudPage page{};

    std::vector<CombatHudRow> rows{};

    std::vector<CombatHudCommand> toggles{}; ///< The protagonist's toggles, numbered on from the commands.
  };

  /** @brief Draw the combat HUD:
   *  - a panel at the top-left with, per row, "<name>  <current> / <max>" (and "  <windup label>"
   *    while winding up), its status labels joined by ", " when it has any, a Health bar and a
   *    timeline bar marking the command point;
   *  - the command bar centred along the bottom: the commands' line, each slot "<glyph> <label>"
   *    with the glyph for @p device, then on a gamepad with more than one page
   *    "L1/R1 <page>/<pages>"; and above it the toggles' line, slot for slot the same. On a
   *    gamepad a slot off the shown page draws only its label;
   *  - and @p banner centred at the top of the viewport when it isn't empty.
   *
   *  Pure render, invoked only while Combat or CombatPaused is on top of the UI stack. */
  void combat_hud_render(platform::Renderer &r, const ui::PanelStyle &style, const ui::NinePatchBorder &border,
                         const CombatHud &hud, core::math::Vec2 viewport, input::InputDevice device,
                         std::string_view banner);

} // namespace corundum::gameplay::screens
