// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/input/physical_input.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace corundum::input {

  namespace {

    /// One enumerator's code paired with its stable JSON name. The three name tables below use
    /// the same shape so one drift guard validates each of them.
    struct NamedCode {
      std::uint16_t code{};

      std::string_view name{};
    };

    /// True when every name is non-empty and every code is strictly greater than the one before.
    /// A forgotten row value-initialises its name to empty; a duplicated or out-of-order row
    /// repeats or decreases a code, so both fail the static_assert a table is checked with.
    template <std::size_t N> constexpr bool names_are_well_formed(const std::array<NamedCode, N> &table) noexcept {
      for (std::size_t i = 0; i < N; ++i) {
        if (table[i].name.empty())
          return false;
        if (i > 0 && table[i].code <= table[i - 1].code)
          return false;
      }
      return true;
    }

    /// Row order is ascending enumerator order; the spelling is the enumerator's own name.
    constexpr std::array<NamedCode, 120> k_key_names{
        {
            {.code = static_cast<std::uint16_t>(Key::Space), .name = "Space"},
            {.code = static_cast<std::uint16_t>(Key::Apostrophe), .name = "Apostrophe"},
            {.code = static_cast<std::uint16_t>(Key::Comma), .name = "Comma"},
            {.code = static_cast<std::uint16_t>(Key::Minus), .name = "Minus"},
            {.code = static_cast<std::uint16_t>(Key::Period), .name = "Period"},
            {.code = static_cast<std::uint16_t>(Key::Slash), .name = "Slash"},
            {.code = static_cast<std::uint16_t>(Key::Digit0), .name = "Digit0"},
            {.code = static_cast<std::uint16_t>(Key::Digit1), .name = "Digit1"},
            {.code = static_cast<std::uint16_t>(Key::Digit2), .name = "Digit2"},
            {.code = static_cast<std::uint16_t>(Key::Digit3), .name = "Digit3"},
            {.code = static_cast<std::uint16_t>(Key::Digit4), .name = "Digit4"},
            {.code = static_cast<std::uint16_t>(Key::Digit5), .name = "Digit5"},
            {.code = static_cast<std::uint16_t>(Key::Digit6), .name = "Digit6"},
            {.code = static_cast<std::uint16_t>(Key::Digit7), .name = "Digit7"},
            {.code = static_cast<std::uint16_t>(Key::Digit8), .name = "Digit8"},
            {.code = static_cast<std::uint16_t>(Key::Digit9), .name = "Digit9"},
            {.code = static_cast<std::uint16_t>(Key::Semicolon), .name = "Semicolon"},
            {.code = static_cast<std::uint16_t>(Key::Equal), .name = "Equal"},
            {.code = static_cast<std::uint16_t>(Key::A), .name = "A"},
            {.code = static_cast<std::uint16_t>(Key::B), .name = "B"},
            {.code = static_cast<std::uint16_t>(Key::C), .name = "C"},
            {.code = static_cast<std::uint16_t>(Key::D), .name = "D"},
            {.code = static_cast<std::uint16_t>(Key::E), .name = "E"},
            {.code = static_cast<std::uint16_t>(Key::F), .name = "F"},
            {.code = static_cast<std::uint16_t>(Key::G), .name = "G"},
            {.code = static_cast<std::uint16_t>(Key::H), .name = "H"},
            {.code = static_cast<std::uint16_t>(Key::I), .name = "I"},
            {.code = static_cast<std::uint16_t>(Key::J), .name = "J"},
            {.code = static_cast<std::uint16_t>(Key::K), .name = "K"},
            {.code = static_cast<std::uint16_t>(Key::L), .name = "L"},
            {.code = static_cast<std::uint16_t>(Key::M), .name = "M"},
            {.code = static_cast<std::uint16_t>(Key::N), .name = "N"},
            {.code = static_cast<std::uint16_t>(Key::O), .name = "O"},
            {.code = static_cast<std::uint16_t>(Key::P), .name = "P"},
            {.code = static_cast<std::uint16_t>(Key::Q), .name = "Q"},
            {.code = static_cast<std::uint16_t>(Key::R), .name = "R"},
            {.code = static_cast<std::uint16_t>(Key::S), .name = "S"},
            {.code = static_cast<std::uint16_t>(Key::T), .name = "T"},
            {.code = static_cast<std::uint16_t>(Key::U), .name = "U"},
            {.code = static_cast<std::uint16_t>(Key::V), .name = "V"},
            {.code = static_cast<std::uint16_t>(Key::W), .name = "W"},
            {.code = static_cast<std::uint16_t>(Key::X), .name = "X"},
            {.code = static_cast<std::uint16_t>(Key::Y), .name = "Y"},
            {.code = static_cast<std::uint16_t>(Key::Z), .name = "Z"},
            {.code = static_cast<std::uint16_t>(Key::LeftBracket), .name = "LeftBracket"},
            {.code = static_cast<std::uint16_t>(Key::Backslash), .name = "Backslash"},
            {.code = static_cast<std::uint16_t>(Key::RightBracket), .name = "RightBracket"},
            {.code = static_cast<std::uint16_t>(Key::GraveAccent), .name = "GraveAccent"},
            {.code = static_cast<std::uint16_t>(Key::World1), .name = "World1"},
            {.code = static_cast<std::uint16_t>(Key::World2), .name = "World2"},
            {.code = static_cast<std::uint16_t>(Key::Escape), .name = "Escape"},
            {.code = static_cast<std::uint16_t>(Key::Enter), .name = "Enter"},
            {.code = static_cast<std::uint16_t>(Key::Tab), .name = "Tab"},
            {.code = static_cast<std::uint16_t>(Key::Backspace), .name = "Backspace"},
            {.code = static_cast<std::uint16_t>(Key::Insert), .name = "Insert"},
            {.code = static_cast<std::uint16_t>(Key::Delete), .name = "Delete"},
            {.code = static_cast<std::uint16_t>(Key::Right), .name = "Right"},
            {.code = static_cast<std::uint16_t>(Key::Left), .name = "Left"},
            {.code = static_cast<std::uint16_t>(Key::Down), .name = "Down"},
            {.code = static_cast<std::uint16_t>(Key::Up), .name = "Up"},
            {.code = static_cast<std::uint16_t>(Key::PageUp), .name = "PageUp"},
            {.code = static_cast<std::uint16_t>(Key::PageDown), .name = "PageDown"},
            {.code = static_cast<std::uint16_t>(Key::Home), .name = "Home"},
            {.code = static_cast<std::uint16_t>(Key::End), .name = "End"},
            {.code = static_cast<std::uint16_t>(Key::CapsLock), .name = "CapsLock"},
            {.code = static_cast<std::uint16_t>(Key::ScrollLock), .name = "ScrollLock"},
            {.code = static_cast<std::uint16_t>(Key::NumLock), .name = "NumLock"},
            {.code = static_cast<std::uint16_t>(Key::PrintScreen), .name = "PrintScreen"},
            {.code = static_cast<std::uint16_t>(Key::Pause), .name = "Pause"},
            {.code = static_cast<std::uint16_t>(Key::F1), .name = "F1"},
            {.code = static_cast<std::uint16_t>(Key::F2), .name = "F2"},
            {.code = static_cast<std::uint16_t>(Key::F3), .name = "F3"},
            {.code = static_cast<std::uint16_t>(Key::F4), .name = "F4"},
            {.code = static_cast<std::uint16_t>(Key::F5), .name = "F5"},
            {.code = static_cast<std::uint16_t>(Key::F6), .name = "F6"},
            {.code = static_cast<std::uint16_t>(Key::F7), .name = "F7"},
            {.code = static_cast<std::uint16_t>(Key::F8), .name = "F8"},
            {.code = static_cast<std::uint16_t>(Key::F9), .name = "F9"},
            {.code = static_cast<std::uint16_t>(Key::F10), .name = "F10"},
            {.code = static_cast<std::uint16_t>(Key::F11), .name = "F11"},
            {.code = static_cast<std::uint16_t>(Key::F12), .name = "F12"},
            {.code = static_cast<std::uint16_t>(Key::F13), .name = "F13"},
            {.code = static_cast<std::uint16_t>(Key::F14), .name = "F14"},
            {.code = static_cast<std::uint16_t>(Key::F15), .name = "F15"},
            {.code = static_cast<std::uint16_t>(Key::F16), .name = "F16"},
            {.code = static_cast<std::uint16_t>(Key::F17), .name = "F17"},
            {.code = static_cast<std::uint16_t>(Key::F18), .name = "F18"},
            {.code = static_cast<std::uint16_t>(Key::F19), .name = "F19"},
            {.code = static_cast<std::uint16_t>(Key::F20), .name = "F20"},
            {.code = static_cast<std::uint16_t>(Key::F21), .name = "F21"},
            {.code = static_cast<std::uint16_t>(Key::F22), .name = "F22"},
            {.code = static_cast<std::uint16_t>(Key::F23), .name = "F23"},
            {.code = static_cast<std::uint16_t>(Key::F24), .name = "F24"},
            {.code = static_cast<std::uint16_t>(Key::F25), .name = "F25"},
            {.code = static_cast<std::uint16_t>(Key::Keypad0), .name = "Keypad0"},
            {.code = static_cast<std::uint16_t>(Key::Keypad1), .name = "Keypad1"},
            {.code = static_cast<std::uint16_t>(Key::Keypad2), .name = "Keypad2"},
            {.code = static_cast<std::uint16_t>(Key::Keypad3), .name = "Keypad3"},
            {.code = static_cast<std::uint16_t>(Key::Keypad4), .name = "Keypad4"},
            {.code = static_cast<std::uint16_t>(Key::Keypad5), .name = "Keypad5"},
            {.code = static_cast<std::uint16_t>(Key::Keypad6), .name = "Keypad6"},
            {.code = static_cast<std::uint16_t>(Key::Keypad7), .name = "Keypad7"},
            {.code = static_cast<std::uint16_t>(Key::Keypad8), .name = "Keypad8"},
            {.code = static_cast<std::uint16_t>(Key::Keypad9), .name = "Keypad9"},
            {.code = static_cast<std::uint16_t>(Key::KeypadDecimal), .name = "KeypadDecimal"},
            {.code = static_cast<std::uint16_t>(Key::KeypadDivide), .name = "KeypadDivide"},
            {.code = static_cast<std::uint16_t>(Key::KeypadMultiply), .name = "KeypadMultiply"},
            {.code = static_cast<std::uint16_t>(Key::KeypadSubtract), .name = "KeypadSubtract"},
            {.code = static_cast<std::uint16_t>(Key::KeypadAdd), .name = "KeypadAdd"},
            {.code = static_cast<std::uint16_t>(Key::KeypadEnter), .name = "KeypadEnter"},
            {.code = static_cast<std::uint16_t>(Key::KeypadEqual), .name = "KeypadEqual"},
            {.code = static_cast<std::uint16_t>(Key::LeftShift), .name = "LeftShift"},
            {.code = static_cast<std::uint16_t>(Key::LeftControl), .name = "LeftControl"},
            {.code = static_cast<std::uint16_t>(Key::LeftAlt), .name = "LeftAlt"},
            {.code = static_cast<std::uint16_t>(Key::LeftSuper), .name = "LeftSuper"},
            {.code = static_cast<std::uint16_t>(Key::RightShift), .name = "RightShift"},
            {.code = static_cast<std::uint16_t>(Key::RightControl), .name = "RightControl"},
            {.code = static_cast<std::uint16_t>(Key::RightAlt), .name = "RightAlt"},
            {.code = static_cast<std::uint16_t>(Key::RightSuper), .name = "RightSuper"},
            {.code = static_cast<std::uint16_t>(Key::Menu), .name = "Menu"},
        },
    };

    constexpr std::array<NamedCode, 3> k_mouse_names{
        {
            {.code = static_cast<std::uint16_t>(MouseButton::Left), .name = "Left"},
            {.code = static_cast<std::uint16_t>(MouseButton::Right), .name = "Right"},
            {.code = static_cast<std::uint16_t>(MouseButton::Middle), .name = "Middle"},
        },
    };

    constexpr std::array<NamedCode, 25> k_gamepad_names{
        {
            {.code = static_cast<std::uint16_t>(GamepadControl::A), .name = "A"},
            {.code = static_cast<std::uint16_t>(GamepadControl::B), .name = "B"},
            {.code = static_cast<std::uint16_t>(GamepadControl::X), .name = "X"},
            {.code = static_cast<std::uint16_t>(GamepadControl::Y), .name = "Y"},
            {.code = static_cast<std::uint16_t>(GamepadControl::LeftBumper), .name = "LeftBumper"},
            {.code = static_cast<std::uint16_t>(GamepadControl::RightBumper), .name = "RightBumper"},
            {.code = static_cast<std::uint16_t>(GamepadControl::Back), .name = "Back"},
            {.code = static_cast<std::uint16_t>(GamepadControl::Start), .name = "Start"},
            {.code = static_cast<std::uint16_t>(GamepadControl::Guide), .name = "Guide"},
            {.code = static_cast<std::uint16_t>(GamepadControl::LeftThumb), .name = "LeftThumb"},
            {.code = static_cast<std::uint16_t>(GamepadControl::RightThumb), .name = "RightThumb"},
            {.code = static_cast<std::uint16_t>(GamepadControl::DpadUp), .name = "DpadUp"},
            {.code = static_cast<std::uint16_t>(GamepadControl::DpadRight), .name = "DpadRight"},
            {.code = static_cast<std::uint16_t>(GamepadControl::DpadDown), .name = "DpadDown"},
            {.code = static_cast<std::uint16_t>(GamepadControl::DpadLeft), .name = "DpadLeft"},
            {.code = static_cast<std::uint16_t>(GamepadControl::LeftStickUp), .name = "LeftStickUp"},
            {.code = static_cast<std::uint16_t>(GamepadControl::LeftStickDown), .name = "LeftStickDown"},
            {.code = static_cast<std::uint16_t>(GamepadControl::LeftStickLeft), .name = "LeftStickLeft"},
            {.code = static_cast<std::uint16_t>(GamepadControl::LeftStickRight), .name = "LeftStickRight"},
            {.code = static_cast<std::uint16_t>(GamepadControl::RightStickUp), .name = "RightStickUp"},
            {.code = static_cast<std::uint16_t>(GamepadControl::RightStickDown), .name = "RightStickDown"},
            {.code = static_cast<std::uint16_t>(GamepadControl::RightStickLeft), .name = "RightStickLeft"},
            {.code = static_cast<std::uint16_t>(GamepadControl::RightStickRight), .name = "RightStickRight"},
            {.code = static_cast<std::uint16_t>(GamepadControl::LeftTrigger), .name = "LeftTrigger"},
            {.code = static_cast<std::uint16_t>(GamepadControl::RightTrigger), .name = "RightTrigger"},
        },
    };

    static_assert(names_are_well_formed(k_key_names));
    static_assert(names_are_well_formed(k_mouse_names));
    static_assert(names_are_well_formed(k_gamepad_names));

    /// The name registered for @p code, or an empty view when none is.
    template <std::size_t N>
    std::string_view find_name(const std::array<NamedCode, N> &table, std::uint16_t code) noexcept {
      for (const NamedCode &entry : table) {
        if (entry.code == code)
          return entry.name;
      }
      return {};
    }

    /// The input named @p name within @p device, or nullopt when none is.
    template <std::size_t N>
    std::optional<PhysicalInput> find_input(const std::array<NamedCode, N> &table, InputDevice device,
                                            std::string_view name) noexcept {
      for (const NamedCode &entry : table) {
        if (entry.name == name)
          return PhysicalInput{.code = entry.code, .device = device};
      }
      return std::nullopt;
    }

    // Radial magnitude gate for the stick, plus a per-axis floor for direction. A radial gate keeps
    // the centre dead while still letting a gentle diagonal register — a per-axis 0.5 threshold
    // required ~0.7 on each axis before a diagonal moved at all.
    constexpr float k_stick_deadzone = 0.5f;
    constexpr float k_stick_direction_threshold = 0.3f;

    // Gamepad triggers rest at -1.0 (released) and read +1.0 fully pressed, so 0.0 is roughly a
    // half-press and serves as the "engaged" threshold.
    constexpr float k_trigger_threshold = 0.f;

    /// True when axis @p value points at least k_stick_direction_threshold in @p positive's
    /// direction, and the owning stick's radial magnitude clears the dead zone.
    bool axis_direction_down(float value, bool positive) noexcept {
      return positive ? value >= k_stick_direction_threshold : value <= -k_stick_direction_threshold;
    }

    /// True when the two-axis stick the given components belong to is outside its dead zone.
    bool stick_engaged(float x, float y) noexcept {
      return std::sqrt((x * x) + (y * y)) >= k_stick_deadzone;
    }

  } // namespace

  bool is_down(const GamepadState &state, GamepadControl control) noexcept {
    const auto index = static_cast<std::size_t>(control);
    if (index < k_gamepad_button_count)
      return state.buttons[index];

    const float left_x = state.axes[static_cast<std::size_t>(GamepadAxis::LeftX)];
    const float left_y = state.axes[static_cast<std::size_t>(GamepadAxis::LeftY)];
    const float right_x = state.axes[static_cast<std::size_t>(GamepadAxis::RightX)];
    const float right_y = state.axes[static_cast<std::size_t>(GamepadAxis::RightY)];

    switch (control) {
      case GamepadControl::LeftStickUp:
        return stick_engaged(left_x, left_y) && axis_direction_down(left_y, false);
      case GamepadControl::LeftStickDown:
        return stick_engaged(left_x, left_y) && axis_direction_down(left_y, true);
      case GamepadControl::LeftStickLeft:
        return stick_engaged(left_x, left_y) && axis_direction_down(left_x, false);
      case GamepadControl::LeftStickRight:
        return stick_engaged(left_x, left_y) && axis_direction_down(left_x, true);
      case GamepadControl::RightStickUp:
        return stick_engaged(right_x, right_y) && axis_direction_down(right_y, false);
      case GamepadControl::RightStickDown:
        return stick_engaged(right_x, right_y) && axis_direction_down(right_y, true);
      case GamepadControl::RightStickLeft:
        return stick_engaged(right_x, right_y) && axis_direction_down(right_x, false);
      case GamepadControl::RightStickRight:
        return stick_engaged(right_x, right_y) && axis_direction_down(right_x, true);
      case GamepadControl::LeftTrigger:
        return state.axes[static_cast<std::size_t>(GamepadAxis::LeftTrigger)] > k_trigger_threshold;
      case GamepadControl::RightTrigger:
        return state.axes[static_cast<std::size_t>(GamepadAxis::RightTrigger)] > k_trigger_threshold;
      default:
        return false; // button controls returned above; Count is not down
    }
  }

  std::string_view device_name(InputDevice device) noexcept {
    switch (device) {
      case InputDevice::Keyboard:
        return "keyboard";
      case InputDevice::Mouse:
        return "mouse";
      case InputDevice::Gamepad:
        return "gamepad";
    }
    return {};
  }

  std::optional<InputDevice> parse_device(std::string_view name) noexcept {
    if (name == "keyboard")
      return InputDevice::Keyboard;
    if (name == "mouse")
      return InputDevice::Mouse;
    if (name == "gamepad")
      return InputDevice::Gamepad;
    return std::nullopt;
  }

  std::string_view name_of(PhysicalInput input) noexcept {
    switch (input.device) {
      case InputDevice::Keyboard:
        return find_name(k_key_names, input.code);
      case InputDevice::Mouse:
        return find_name(k_mouse_names, input.code);
      case InputDevice::Gamepad:
        return find_name(k_gamepad_names, input.code);
    }
    return {};
  }

  std::optional<PhysicalInput> parse_physical_input(InputDevice device, std::string_view name) noexcept {
    switch (device) {
      case InputDevice::Keyboard:
        return find_input(k_key_names, device, name);
      case InputDevice::Mouse:
        return find_input(k_mouse_names, device, name);
      case InputDevice::Gamepad:
        return find_input(k_gamepad_names, device, name);
    }
    return std::nullopt;
  }

} // namespace corundum::input
