// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/input/actions.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/ui/input_glyph.hpp>

namespace {

  using corundum::input::Action;
  using corundum::input::InputDevice;
  using corundum::ui::input_glyph;

} // namespace

TEST_CASE("input_glyph: keyboard uses the key name") {
  CHECK(input_glyph(Action::Cancel, InputDevice::Keyboard) == "Esc");
  CHECK(input_glyph(Action::Journal, InputDevice::Keyboard) == "J");
  CHECK(input_glyph(Action::Select, InputDevice::Keyboard) == "Enter");
  CHECK(input_glyph(Action::Menu, InputDevice::Keyboard) == "Esc");
  CHECK(input_glyph(Action::TabNext, InputDevice::Keyboard) == "]");
  CHECK(input_glyph(Action::TabPrev, InputDevice::Keyboard) == "[");
  CHECK(input_glyph(Action::SubTabNext, InputDevice::Keyboard) == ".");
  CHECK(input_glyph(Action::SubTabPrev, InputDevice::Keyboard) == ",");
}

TEST_CASE("input_glyph: gamepad uses the button name") {
  CHECK(input_glyph(Action::Cancel, InputDevice::Gamepad) == "B");
  CHECK(input_glyph(Action::Activate, InputDevice::Gamepad) == "A");
  CHECK(input_glyph(Action::Menu, InputDevice::Gamepad) == "Start");
  CHECK(input_glyph(Action::Hub, InputDevice::Gamepad) == "Y");
  CHECK(input_glyph(Action::TabNext, InputDevice::Gamepad) == "R1");
  CHECK(input_glyph(Action::TabPrev, InputDevice::Gamepad) == "L1");
  CHECK(input_glyph(Action::SubTabNext, InputDevice::Gamepad) == "R2");
  CHECK(input_glyph(Action::SubTabPrev, InputDevice::Gamepad) == "L2");
}

TEST_CASE("input_glyph: the per-screen gamepad buttons are gone with the hub") {
  CHECK(input_glyph(Action::Journal, InputDevice::Gamepad) == "?");
  CHECK(input_glyph(Action::Codex, InputDevice::Gamepad) == "?");
  CHECK(input_glyph(Action::Map, InputDevice::Gamepad) == "?");
  CHECK(input_glyph(Action::Inventory, InputDevice::Gamepad) == "?");
}

TEST_CASE("input_glyph: mouse names the button") {
  CHECK(input_glyph(Action::Activate, InputDevice::Mouse) == "LMB");
  CHECK(input_glyph(Action::Cancel, InputDevice::Mouse) == "RMB");
}

TEST_CASE("input_glyph: an unmapped action falls back to ? rather than empty") {
  CHECK(input_glyph(Action::Quit, InputDevice::Gamepad) == "?");
  CHECK(input_glyph(Action::QuickSave, InputDevice::Mouse) == "?");
}
