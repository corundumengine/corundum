// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/input/action_resolver.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/bindings.hpp>
#include <corundum/input/input_mapper.hpp>
#include <corundum/input/physical_input.hpp>

#include <cstddef>
#include <string>

using corundum::input::Action;
using corundum::input::Bindings;
using corundum::input::default_bindings;
using corundum::input::GamepadAxis;
using corundum::input::GamepadControl;
using corundum::input::GamepadState;
using corundum::input::InputDevice;
using corundum::input::InputMapper;
using corundum::input::InputState;
using corundum::input::k_max_input_sources;
using corundum::input::Key;
using corundum::input::MouseButton;
using corundum::input::physical;

namespace {

  /// One full poll: begin, feed the mapper, then accumulate into a fresh state.
  template <typename Feed> InputState run(InputMapper &mapper, Feed feed) {
    mapper.begin_poll();
    feed(mapper);
    InputState state{};
    mapper.end_poll(state);
    return state;
  }

  GamepadState gamepad_with(GamepadControl control) {
    GamepadState state{};
    state.buttons[static_cast<std::size_t>(control)] = true;
    return state;
  }

} // namespace

TEST_CASE("InputMapper: a key press drives its action") {
  InputMapper mapper;

  const InputState pressed = run(mapper, [](InputMapper &m) { m.key(Key::W, true); });
  CHECK(pressed.is_pressed(Action::MoveUp));
  CHECK(pressed.is_held(Action::MoveUp));

  const InputState released = run(mapper, [](InputMapper &m) { m.key(Key::W, false); });
  CHECK_FALSE(released.is_pressed(Action::MoveUp));
  CHECK_FALSE(released.is_held(Action::MoveUp));
}

TEST_CASE("InputMapper: a second source does not re-raise the press") {
  InputMapper mapper;
  run(mapper, [](InputMapper &m) { m.key(Key::W, true); });

  const InputState both = run(mapper, [](InputMapper &m) { m.key(Key::Up, true); });
  CHECK(both.is_held(Action::MoveUp));
  CHECK_FALSE(both.is_pressed(Action::MoveUp));

  const InputState released_w = run(mapper, [](InputMapper &m) { m.key(Key::W, false); });
  CHECK(released_w.is_held(Action::MoveUp));

  const InputState released_up = run(mapper, [](InputMapper &m) { m.key(Key::Up, false); });
  CHECK_FALSE(released_up.is_held(Action::MoveUp));
}

TEST_CASE("InputMapper: an unbound key changes nothing") {
  InputMapper mapper;
  const InputState state = run(mapper, [](InputMapper &m) { m.key(Key::F12, true); });

  for (std::size_t i = 0; i < static_cast<std::size_t>(Action::Count); ++i)
    CHECK_FALSE(state.is_held(static_cast<Action>(i)));
  CHECK_FALSE(state.is_pressed(Action::Select));
}

TEST_CASE("InputMapper: mouse buttons and the click signal") {
  InputMapper mapper;

  const InputState left = run(mapper, [](InputMapper &m) { m.mouse_button(MouseButton::Left, true); });
  CHECK(left.is_pressed(Action::Select));
  CHECK(left.mouse_click_pressed);

  const InputState right = run(mapper, [](InputMapper &m) { m.mouse_button(MouseButton::Right, true); });
  CHECK_FALSE(right.is_pressed(Action::Select));
  CHECK_FALSE(right.mouse_click_pressed);
}

TEST_CASE("InputMapper: cursor and scroll") {
  InputMapper mapper;
  const InputState state = run(mapper, [](InputMapper &m) {
    m.scroll(1.f);
    m.scroll(1.f);
    m.cursor(10.f, 20.f);
  });

  CHECK(state.scroll_delta_y == doctest::Approx(2.f));
  CHECK(state.mouse_x == doctest::Approx(10.f));
  CHECK(state.mouse_y == doctest::Approx(20.f));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): CHECK expands to branches.
TEST_CASE("InputMapper: begin_poll clears edges but keeps held") {
  InputMapper mapper;

  const InputState first = run(mapper, [](InputMapper &m) {
    m.key(Key::W, true);
    m.scroll(1.f);
    m.mouse_button(MouseButton::Left, true);
  });
  CHECK(first.is_pressed(Action::MoveUp));
  CHECK(first.mouse_click_pressed);
  CHECK(first.scroll_delta_y == doctest::Approx(1.f));

  const InputState second = run(mapper, [](InputMapper &) {});
  CHECK_FALSE(second.is_pressed(Action::MoveUp));
  CHECK(second.is_held(Action::MoveUp));
  CHECK_FALSE(second.mouse_click_pressed);
  CHECK(second.scroll_delta_y == doctest::Approx(0.f));
}

TEST_CASE("InputMapper: gamepad buttons, stick and trigger") {
  InputMapper mapper;

  const InputState button = run(mapper, [](InputMapper &m) { m.gamepad(gamepad_with(GamepadControl::A)); });
  CHECK(button.is_pressed(Action::Select));

  const InputState repeat = run(mapper, [](InputMapper &m) { m.gamepad(gamepad_with(GamepadControl::A)); });
  CHECK_FALSE(repeat.is_pressed(Action::Select));
  CHECK(repeat.is_held(Action::Select));

  GamepadState stick{};
  stick.axes[static_cast<std::size_t>(GamepadAxis::LeftY)] = -0.9f;
  CHECK(run(mapper, [&](InputMapper &m) { m.gamepad(stick); }).is_held(Action::MoveUp));

  GamepadState trigger{};
  trigger.axes[static_cast<std::size_t>(GamepadAxis::RightTrigger)] = 0.5f;
  CHECK(run(mapper, [&](InputMapper &m) { m.gamepad(trigger); }).is_pressed(Action::ZoomIn));
}

TEST_CASE("InputMapper: gamepad_absent releases the held controls") {
  InputMapper mapper;
  run(mapper, [](InputMapper &m) { m.gamepad(gamepad_with(GamepadControl::A)); });
  CHECK(mapper.gamepad_connected());

  const InputState absent = run(mapper, [](InputMapper &m) { m.gamepad_absent(); });
  CHECK_FALSE(absent.is_held(Action::Select));
  CHECK_FALSE(mapper.gamepad_connected());
}

TEST_CASE("InputMapper: last_device tracks presses only") {
  InputMapper mapper;
  CHECK(mapper.last_device() == InputDevice::Keyboard);

  run(mapper, [](InputMapper &m) { m.gamepad(gamepad_with(GamepadControl::A)); });
  CHECK(mapper.last_device() == InputDevice::Gamepad);

  run(mapper, [](InputMapper &m) { m.key(Key::W, false); });
  CHECK(mapper.last_device() == InputDevice::Gamepad);

  run(mapper, [](InputMapper &m) { m.mouse_button(MouseButton::Right, true); });
  CHECK(mapper.last_device() == InputDevice::Mouse);
}

TEST_CASE("InputMapper: capture takes the next press") {
  InputMapper mapper;
  mapper.begin_capture();
  CHECK(mapper.is_capturing());

  const InputState captured = run(mapper, [](InputMapper &m) { m.key(Key::Escape, true); });
  CHECK_FALSE(captured.is_pressed(Action::Cancel));
  CHECK_FALSE(mapper.is_capturing());
  CHECK(mapper.take_captured() == physical(Key::Escape));
  CHECK_FALSE(mapper.take_captured().has_value());
}

TEST_CASE("InputMapper: capture takes a gamepad press") {
  InputMapper mapper;
  mapper.begin_capture();

  const InputState captured = run(mapper, [](InputMapper &m) { m.gamepad(gamepad_with(GamepadControl::B)); });
  CHECK_FALSE(captured.is_pressed(Action::Cancel));
  CHECK(mapper.take_captured() == physical(GamepadControl::B));
}

TEST_CASE("InputMapper: capture takes a mouse press without a click") {
  InputMapper mapper;
  mapper.begin_capture();

  const InputState captured = run(mapper, [](InputMapper &m) { m.mouse_button(MouseButton::Left, true); });
  CHECK_FALSE(captured.mouse_click_pressed);
  CHECK_FALSE(captured.is_pressed(Action::Select));
  CHECK(mapper.take_captured() == physical(MouseButton::Left));
}

TEST_CASE("InputMapper: cancel_capture restores routing") {
  InputMapper mapper;
  mapper.begin_capture();
  mapper.cancel_capture();

  const InputState state = run(mapper, [](InputMapper &m) { m.key(Key::Escape, true); });
  CHECK(state.is_pressed(Action::Cancel));
}

TEST_CASE("InputMapper: set_bindings rejects an oversized table") {
  InputMapper mapper;
  const Bindings original = mapper.bindings();
  const Bindings too_many(k_max_input_sources + 1);

  const auto result = mapper.set_bindings(too_many);
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().find(std::to_string(k_max_input_sources)) != std::string::npos);
  CHECK(mapper.bindings() == original);
}

TEST_CASE("InputMapper: set_bindings swaps the table") {
  InputMapper mapper;
  REQUIRE(mapper.set_bindings(Bindings{{Action::MoveUp, physical(Key::Q)}}).has_value());

  const InputState via_w = run(mapper, [](InputMapper &m) { m.key(Key::W, true); });
  CHECK_FALSE(via_w.is_held(Action::MoveUp));

  const InputState via_q = run(mapper, [](InputMapper &m) { m.key(Key::Q, true); });
  CHECK(via_q.is_held(Action::MoveUp));
}

TEST_CASE("InputMapper: set_bindings releases what the old table held") {
  InputMapper mapper;
  run(mapper, [](InputMapper &m) { m.key(Key::W, true); });
  REQUIRE(mapper.set_bindings(default_bindings()).has_value());

  const InputState state = run(mapper, [](InputMapper &) {});
  CHECK_FALSE(state.is_held(Action::MoveUp));
}

TEST_CASE("InputMapper: a release for a rebound key never presses its new action") {
  InputMapper mapper;
  run(mapper, [](InputMapper &m) { m.key(Key::W, true); });
  REQUIRE(mapper.set_bindings(Bindings{{Action::Cancel, physical(Key::W)}}).has_value());

  const InputState state = run(mapper, [](InputMapper &m) { m.key(Key::W, false); });
  CHECK_FALSE(state.is_pressed(Action::Cancel));
  CHECK_FALSE(state.is_held(Action::Cancel));
}
