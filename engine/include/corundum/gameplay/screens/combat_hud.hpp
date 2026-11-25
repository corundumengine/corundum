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

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace corundum::gameplay::screens {

/** One combatant's row on the combat HUD; combat::Battle refreshes the numbers every step. */
struct CombatHudRow {
  rules::Health health{};

  bool is_hostile{};

  /** The fight's chosen hostile; its name draws in the selected colour. */
  bool is_target{};

  /** Copied from rules::Fight::names when the fight starts. */
  std::string name{};
};

/** One command-bar slot: the actions that press it, and what it does. */
struct CombatHudCommand {
  /** The key that presses it. */
  input::Action action{input::Action::CombatCommand1};

  /** The action whose gamepad button presses it while its page shows; nullopt off the page. */
  std::optional<input::Action> gamepad_action{};

  /** Drawn dimmed when false. */
  bool is_available{true};

  /** Drawn highlighted: the queued command. */
  bool is_highlighted{};

  std::string label{};
};

/** Which three slots a gamepad's A, Y and B press: slots 3 × index to 3 × index + 2. */
struct CombatHudPage {
  std::size_t count{1};

  /** Below count. */
  std::size_t index{};
};

/** What the combat HUD shows: the command bar and one row per combatant. */
struct CombatHud {
  /** Attack, then the protagonist's commands. */
  std::vector<CombatHudCommand> commands{};

  CombatHudPage page{};

  std::vector<CombatHudRow> rows{};
};

/** Draw the combat HUD:
 *  - a panel at the top-left with, per row, "<name>  <current> / <max>" and a Health bar;
 *  - the command bar centred along the bottom: each slot "<glyph> <label>" with the glyph for
 *    `device`, then on a gamepad with more than one page "L1/R1 <page>/<pages>". On a gamepad a
 *    slot off the shown page draws only its label;
 *  - and `banner` centred at the top of the viewport when it isn't empty.
 *
 *  Pure render, invoked only while Combat or CombatPaused is on top of the UI stack.
 */
void combat_hud_render(platform::Renderer &renderer, const ui::PanelStyle &style, const ui::NinePatchBorder &border,
                       const CombatHud &hud, core::math::Vec2 viewport, input::InputDevice device,
                       std::string_view banner);

} // namespace corundum::gameplay::screens
