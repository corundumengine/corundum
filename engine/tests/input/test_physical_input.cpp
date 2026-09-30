// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/input/physical_input.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>

using corundum::input::device_name;
using corundum::input::GamepadAxis;
using corundum::input::GamepadControl;
using corundum::input::GamepadState;
using corundum::input::InputDevice;
using corundum::input::is_down;
using corundum::input::Key;
using corundum::input::MouseButton;
using corundum::input::name_of;
using corundum::input::parse_device;
using corundum::input::parse_physical_input;
using corundum::input::physical;

// ── Name round-trips ─────────────────────────────────────────────────────────

TEST_CASE("physical input: keyboard names round-trip") {
  std::size_t found{};
  for (std::uint16_t code = 0; code <= 400; ++code) {
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): probing codes with no enumerator is the point.
    const auto input = physical(static_cast<Key>(code));
    const std::string_view name = name_of(input);
    if (name.empty())
      continue;
    ++found;
    CHECK(parse_physical_input(InputDevice::Keyboard, name) == input);
  }
  CHECK(found == 120);
}

TEST_CASE("physical input: mouse names round-trip") {
  std::size_t found{};
  for (std::uint16_t code = 0; code < 8; ++code) {
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): probing codes with no enumerator is the point.
    const auto input = physical(static_cast<MouseButton>(code));
    const std::string_view name = name_of(input);
    if (name.empty())
      continue;
    ++found;
    CHECK(parse_physical_input(InputDevice::Mouse, name) == input);
  }
  CHECK(found == 3);
}

TEST_CASE("physical input: gamepad names round-trip") {
  std::size_t found{};
  for (std::uint16_t code = 0; code < static_cast<std::uint16_t>(GamepadControl::Count); ++code) {
    const auto input = physical(static_cast<GamepadControl>(code));
    const std::string_view name = name_of(input);
    REQUIRE_FALSE(name.empty());
    ++found;
    CHECK(parse_physical_input(InputDevice::Gamepad, name) == input);
  }
  CHECK(found == 25);
}

TEST_CASE("physical input: parsing is scoped to the device") {
  CHECK_FALSE(parse_physical_input(InputDevice::Keyboard, "LeftStickUp").has_value());
  CHECK(parse_physical_input(InputDevice::Gamepad, "A") == physical(GamepadControl::A));
}

TEST_CASE("physical input: device names round-trip") {
  for (const InputDevice device : {InputDevice::Keyboard, InputDevice::Mouse, InputDevice::Gamepad}) {
    CHECK(parse_device(device_name(device)) == device);
  }
  CHECK_FALSE(parse_device("joystick").has_value());
}

// ── is_down: sticks ──────────────────────────────────────────────────────────

TEST_CASE("physical input: a stick direction reads its axis") {
  GamepadState state{};
  state.axes[static_cast<std::size_t>(GamepadAxis::LeftY)] = -0.9f;

  CHECK(is_down(state, GamepadControl::LeftStickUp));
  CHECK_FALSE(is_down(state, GamepadControl::LeftStickDown));
  CHECK_FALSE(is_down(state, GamepadControl::LeftStickLeft));
  CHECK_FALSE(is_down(state, GamepadControl::LeftStickRight));
}

TEST_CASE("physical input: a centered stick is dead") {
  GamepadState state{};
  state.axes[static_cast<std::size_t>(GamepadAxis::LeftX)] = 0.35f;
  state.axes[static_cast<std::size_t>(GamepadAxis::LeftY)] = 0.35f;

  CHECK_FALSE(is_down(state, GamepadControl::LeftStickUp));
  CHECK_FALSE(is_down(state, GamepadControl::LeftStickDown));
  CHECK_FALSE(is_down(state, GamepadControl::LeftStickLeft));
  CHECK_FALSE(is_down(state, GamepadControl::LeftStickRight));
}

TEST_CASE("physical input: a gentle diagonal registers on both axes") {
  GamepadState state{};
  state.axes[static_cast<std::size_t>(GamepadAxis::LeftX)] = -0.4f;
  state.axes[static_cast<std::size_t>(GamepadAxis::LeftY)] = -0.4f;

  CHECK(is_down(state, GamepadControl::LeftStickUp));
  CHECK(is_down(state, GamepadControl::LeftStickLeft));
}

TEST_CASE("physical input: the right stick does not drive left-stick controls") {
  GamepadState state{};
  state.axes[static_cast<std::size_t>(GamepadAxis::RightY)] = -0.9f;

  CHECK_FALSE(is_down(state, GamepadControl::LeftStickUp));
  CHECK(is_down(state, GamepadControl::RightStickUp));
}

// ── is_down: triggers and buttons ────────────────────────────────────────────

TEST_CASE("physical input: triggers rest up and engage past half-press") {
  GamepadState state{};
  CHECK_FALSE(is_down(state, GamepadControl::LeftTrigger));
  CHECK_FALSE(is_down(state, GamepadControl::RightTrigger));

  state.axes[static_cast<std::size_t>(GamepadAxis::RightTrigger)] = 0.5f;
  CHECK(is_down(state, GamepadControl::RightTrigger));
  CHECK_FALSE(is_down(state, GamepadControl::LeftTrigger));
}

TEST_CASE("physical input: buttons read directly") {
  GamepadState state{};
  state.buttons[static_cast<std::size_t>(GamepadControl::B)] = true;

  CHECK(is_down(state, GamepadControl::B));
  CHECK_FALSE(is_down(state, GamepadControl::A));
}
