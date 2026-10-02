// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/input/actions.hpp>
#include <corundum/input/input_intent.hpp>
#include <corundum/input/physical_input.hpp>

#include <cstddef>

namespace {

  using corundum::input::Action;
  using corundum::input::InputDevice;
  using corundum::input::InputIntent;
  using corundum::input::InputState;
  using corundum::input::make_input_intent;

  InputIntent intent_with(Action action) {
    InputState state{};
    state.pressed.set(static_cast<std::size_t>(action));
    return make_input_intent(state, InputDevice::Keyboard);
  }

} // namespace

TEST_CASE("make_input_intent: empty state decodes to an all-false intent") {
  const InputIntent intent = make_input_intent(InputState{}, InputDevice::Gamepad);

  CHECK(intent.navigate_x == 0);
  CHECK(intent.navigate_y == 0);
  CHECK_FALSE(intent.select);
  CHECK_FALSE(intent.activate);
  CHECK_FALSE(intent.back);
  CHECK_FALSE(intent.next_tab);
  CHECK_FALSE(intent.prev_tab);
  CHECK_FALSE(intent.cursor_clicked);
  CHECK(intent.last_device == InputDevice::Gamepad);
}

TEST_CASE("make_input_intent: navigation presses step each axis, one per press") {
  CHECK(intent_with(Action::MoveLeft).navigate_x == -1);
  CHECK(intent_with(Action::MoveRight).navigate_x == 1);
  CHECK(intent_with(Action::MoveUp).navigate_y == -1);
  CHECK(intent_with(Action::MoveDown).navigate_y == 1);
}

TEST_CASE("make_input_intent: opposite presses cancel on their axis") {
  InputState state{};
  state.pressed.set(static_cast<std::size_t>(Action::MoveLeft));
  state.pressed.set(static_cast<std::size_t>(Action::MoveRight));

  const InputIntent intent = make_input_intent(state, InputDevice::Keyboard);
  CHECK(intent.navigate_x == 0);
}

TEST_CASE("make_input_intent: Select raises select and the activate fallback") {
  const InputIntent intent = intent_with(Action::Select);
  CHECK(intent.select);
  CHECK(intent.activate);
  CHECK_FALSE(intent.back);
}

TEST_CASE("make_input_intent: Activate raises activate without select") {
  const InputIntent intent = intent_with(Action::Activate);
  CHECK(intent.activate);
  CHECK_FALSE(intent.select);
}

TEST_CASE("make_input_intent: Cancel decodes to back") {
  CHECK(intent_with(Action::Cancel).back);
}

TEST_CASE("make_input_intent: TabNext and TabPrev decode to their own flags") {
  const InputIntent next = intent_with(Action::TabNext);
  CHECK(next.next_tab);
  CHECK_FALSE(next.prev_tab);

  const InputIntent prev = intent_with(Action::TabPrev);
  CHECK(prev.prev_tab);
  CHECK_FALSE(prev.next_tab);
}

TEST_CASE("make_input_intent: carries cursor, click, and scroll") {
  InputState state{};
  state.mouse_x = 12.f;
  state.mouse_y = 34.f;
  state.mouse_click_pressed = true;
  state.scroll_delta_y = 2.f;

  const InputIntent intent = make_input_intent(state, InputDevice::Mouse);
  CHECK(intent.cursor_x == 12.f);
  CHECK(intent.cursor_y == 34.f);
  CHECK(intent.cursor_clicked);
  CHECK(intent.scroll_y == 2.f);
  CHECK(intent.last_device == InputDevice::Mouse);
}
