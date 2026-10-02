// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/input/actions.hpp>
#include <corundum/input/physical_input.hpp>

#include <string_view>

namespace corundum::ui {

  /** @brief The on-screen hint for @p action on @p device.
   *
   *  Lets a prompt read the same way whichever device the player last touched — "Esc" on a
   *  keyboard, "B" on a gamepad, "RMB" on a mouse. Returns "?" for an action with no hint on
   *  that device; never empty.
   *
   *  @param action Action the hint stands for.
   *  @param device Device of the player's most recent press, from InputIntent::last_device.
   */
  [[nodiscard]] std::string_view input_glyph(input::Action action, input::InputDevice device) noexcept;

} // namespace corundum::ui
