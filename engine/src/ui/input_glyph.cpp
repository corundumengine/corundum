// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/ui/input_glyph.hpp>

#include <corundum/input/actions.hpp>
#include <corundum/input/physical_input.hpp>

#include <string_view>

namespace corundum::ui {

  namespace {

    std::string_view keyboard_glyph(input::Action action) noexcept {
      using input::Action;
      switch (action) {
        case Action::MoveUp:
          return "W";
        case Action::MoveDown:
          return "S";
        case Action::MoveLeft:
          return "A";
        case Action::MoveRight:
          return "D";
        case Action::Select:
        case Action::Activate:
          return "Enter";
        case Action::Cancel:
          return "Esc";
        case Action::Quit:
          return "Q";
        case Action::ZoomIn:
          return "=";
        case Action::ZoomOut:
          return "-";
        case Action::Inventory:
          return "I";
        case Action::Journal:
          return "J";
        case Action::Codex:
          return "C";
        case Action::Map:
          return "M";
        case Action::Menu:
          return "Esc";
        case Action::Hub:
          return "?"; // gamepad-only; keyboard opens hub tabs with I/J/C/M
        case Action::TabNext:
          return "]";
        case Action::TabPrev:
          return "[";
        case Action::SubTabNext:
          return ".";
        case Action::SubTabPrev:
          return ",";
        case Action::QuickSave:
          return "F5";
        case Action::QuickLoad:
          return "F9";
        case Action::Count:
          break;
      }
      return "?";
    }

    std::string_view mouse_glyph(input::Action action) noexcept {
      using input::Action;
      switch (action) {
        case Action::Select:
        case Action::Activate:
          return "LMB";
        case Action::Cancel:
          return "RMB";
        case Action::ZoomIn:
        case Action::ZoomOut:
          return "Wheel";
        default:
          break;
      }
      return "?";
    }

    std::string_view gamepad_glyph(input::Action action) noexcept {
      using input::Action;
      switch (action) {
        case Action::Select:
        case Action::Activate:
          return "A";
        case Action::Cancel:
          return "B";
        case Action::Hub:
          return "Y";
        case Action::Menu:
          return "Start";
        case Action::TabNext:
          return "R1";
        case Action::TabPrev:
          return "L1";
        case Action::SubTabNext:
          return "R2";
        case Action::SubTabPrev:
          return "L2";
        case Action::MoveUp:
          return "D-Up";
        case Action::MoveDown:
          return "D-Down";
        case Action::MoveLeft:
          return "D-Left";
        case Action::MoveRight:
          return "D-Right";
        case Action::ZoomIn:
          return "R2";
        case Action::ZoomOut:
          return "L2";
        default:
          break;
      }
      return "?";
    }

  } // namespace

  std::string_view input_glyph(input::Action action, input::InputDevice device) noexcept {
    switch (device) {
      case input::InputDevice::Keyboard:
        return keyboard_glyph(action);
      case input::InputDevice::Mouse:
        return mouse_glyph(action);
      case input::InputDevice::Gamepad:
        return gamepad_glyph(action);
    }
    return "?";
  }

} // namespace corundum::ui
