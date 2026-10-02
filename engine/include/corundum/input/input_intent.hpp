// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/input/actions.hpp>
#include <corundum/input/physical_input.hpp>

namespace corundum::input {

  /** @brief One frame of abstract navigational intent, decoded from raw InputState.
   *
   *  This is the "Input → Intent" seam: a small value type a screen consumes in place of
   *  the device that produced the input, so gamepad, keyboard, and mouse+keyboard all drive
   *  the same code path. Built once per fixed step by make_input_intent().
   *
   *  `select` and `activate` are separate fields with separate consumers: `select` is the
   *  dialogue/confirm press (Action::Select), while `activate` is the world/menu "use this
   *  thing" press (Action::Activate). `activate` falls back to Select so a binding table saved
   *  before Activate existed still drives the world.
   */
  struct InputIntent {
    /** @brief -1/0/+1 pressed step along each navigation axis; a press moves one row/column. */
    int navigate_x{};
    int navigate_y{};

    /** @brief Action::Select was pressed this step (dialogue/confirm). */
    bool select{};

    /** @brief A world/UI "use this thing" press this step.
     *
     *  Raised by Action::Activate or, so a binding table saved before that action existed
     *  still drives the world, by Action::Select. Distinct from @c select: a screen that means
     *  dialogue/confirm reads @c select, while the world and menus read this. */
    bool activate{};

    /** @brief Action::Cancel was pressed this step. */
    bool back{};

    /** @brief Action::TabNext was pressed this step (settings/journal tab forward). */
    bool next_tab{};

    /** @brief Action::TabPrev was pressed this step (settings/journal tab back). */
    bool prev_tab{};

    /** @brief Cursor position in window pixels. */
    float cursor_x{};

    float cursor_y{};

    /** @brief Accumulated scroll wheel delta this step (positive = away from the user). */
    float scroll_y{};

    /** @brief True only on the step the left mouse button was pressed. */
    bool cursor_clicked{};

    /** @brief Device of the most recent press; selects which on-screen glyph a prompt shows. */
    InputDevice last_device{InputDevice::Keyboard};
  };

  /** @brief Decode @p state into abstract navigational intent.
   *
   *  @param state       Per-frame input snapshot.
   *  @param last_device Device of the most recent press, from InputMapper::last_device().
   *  @return The decoded intent. Navigation steps are one per press, not held level.
   */
  [[nodiscard]] InputIntent make_input_intent(const InputState &state, InputDevice last_device) noexcept;

} // namespace corundum::input
