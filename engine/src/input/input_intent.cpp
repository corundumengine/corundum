// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/input/input_intent.hpp>

#include <corundum/input/actions.hpp>
#include <corundum/input/physical_input.hpp>

namespace corundum::input {

  InputIntent make_input_intent(const InputState &state, InputDevice last_device) noexcept {
    InputIntent intent{};
    intent.last_device = last_device;
    intent.cursor_x = state.mouse_x;
    intent.cursor_y = state.mouse_y;
    intent.scroll_y = state.scroll_delta_y;
    intent.cursor_clicked = state.mouse_click_pressed;
    intent.mouse_moved = state.mouse_x != state.prev_mouse_x || state.mouse_y != state.prev_mouse_y;
    intent.select = state.is_pressed(Action::Select);
    intent.activate = state.is_pressed(Action::Activate) || state.is_pressed(Action::Select);
    intent.back = state.is_pressed(Action::Cancel);
    intent.next_tab = state.is_pressed(Action::TabNext);
    intent.prev_tab = state.is_pressed(Action::TabPrev);
    intent.next_sub_tab = state.is_pressed(Action::SubTabNext);
    intent.prev_sub_tab = state.is_pressed(Action::SubTabPrev);

    if (state.is_pressed(Action::MoveLeft))
      --intent.navigate_x;
    if (state.is_pressed(Action::MoveRight))
      ++intent.navigate_x;
    if (state.is_pressed(Action::MoveUp))
      --intent.navigate_y;
    if (state.is_pressed(Action::MoveDown))
      ++intent.navigate_y;

    return intent;
  }

} // namespace corundum::input
