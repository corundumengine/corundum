// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace corundum::input {

  /** @brief Keyboard key, named by its position on a US layout.
   *
   *  Values equal GLFW's key tokens, so the desktop backend converts with a cast. The
   *  enumerator spelling is the key's stable JSON name (see name_of()); the label printed
   *  on the player's own layout comes from platform::Window::input_label().
   */
  // NOLINTNEXTLINE(readability-enum-initial-value): values are a contract with GLFW's key tokens.
  enum class Key : std::uint16_t {
    Space = 32,
    Apostrophe = 39,
    Comma = 44,
    Minus = 45,
    Period = 46,
    Slash = 47,
    Digit0 = 48,
    Digit1,
    Digit2,
    Digit3,
    Digit4,
    Digit5,
    Digit6,
    Digit7,
    Digit8,
    Digit9,
    Semicolon = 59,
    Equal = 61,
    A = 65,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,
    LeftBracket = 91,
    Backslash = 92,
    RightBracket = 93,
    GraveAccent = 96,
    World1 = 161,
    World2 = 162,
    Escape = 256,
    Enter,
    Tab,
    Backspace,
    Insert,
    Delete,
    Right,
    Left,
    Down,
    Up,
    PageUp,
    PageDown,
    Home,
    End,
    CapsLock = 280,
    ScrollLock,
    NumLock,
    PrintScreen,
    Pause,
    F1 = 290,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
    F13,
    F14,
    F15,
    F16,
    F17,
    F18,
    F19,
    F20,
    F21,
    F22,
    F23,
    F24,
    F25,
    Keypad0 = 320,
    Keypad1,
    Keypad2,
    Keypad3,
    Keypad4,
    Keypad5,
    Keypad6,
    Keypad7,
    Keypad8,
    Keypad9,
    KeypadDecimal,
    KeypadDivide,
    KeypadMultiply,
    KeypadSubtract,
    KeypadAdd,
    KeypadEnter,
    KeypadEqual,
    LeftShift = 340,
    LeftControl,
    LeftAlt,
    LeftSuper,
    RightShift,
    RightControl,
    RightAlt,
    RightSuper,
    Menu,
  };

  /** @brief Mouse button. Values equal GLFW's button indices. */
  enum class MouseButton : std::uint8_t {
    Left,
    Right,
    Middle,
  };

  /** @brief A gamepad control with a digital reading.
   *
   *  The first k_gamepad_button_count values are buttons in SDL gamepad-database order (equal to
   *  GLFW_GAMEPAD_BUTTON_*), so they name the same physical control on every mapped pad. The rest
   *  are stick directions and triggers, which read as down past their thresholds (see is_down()).
   */
  enum class GamepadControl : std::uint8_t {
    A,
    B,
    X,
    Y,
    LeftBumper,
    RightBumper,
    Back,
    Start,
    Guide,
    LeftThumb,
    RightThumb,
    DpadUp,
    DpadRight,
    DpadDown,
    DpadLeft,
    LeftStickUp,
    LeftStickDown,
    LeftStickLeft,
    LeftStickRight,
    RightStickUp,
    RightStickDown,
    RightStickLeft,
    RightStickRight,
    LeftTrigger,
    RightTrigger,
    Count,
  };

  /** @brief Number of GamepadControl values that are plain buttons. */
  constexpr std::size_t k_gamepad_button_count = 15;

  /** @brief Number of GamepadControl values. */
  constexpr std::size_t k_gamepad_control_count = static_cast<std::size_t>(GamepadControl::Count);

  /** @brief Gamepad analogue axis. Values equal GLFW_GAMEPAD_AXIS_*. */
  enum class GamepadAxis : std::uint8_t {
    LeftX,
    LeftY,
    RightX,
    RightY,
    LeftTrigger,
    RightTrigger,
  };

  /** @brief Number of GamepadAxis values. */
  constexpr std::size_t k_gamepad_axis_count = 6;

  /** @brief One poll's snapshot of a mapped gamepad, independent of the backend. */
  struct GamepadState {
    /** @brief Indexed by GamepadAxis, each in [-1, 1]. Stick +Y points down; triggers rest at -1. */
    std::array<float, k_gamepad_axis_count> axes{0.f, 0.f, 0.f, 0.f, -1.f, -1.f};

    /** @brief Indexed by the first k_gamepad_button_count GamepadControl values. */
    std::array<bool, k_gamepad_button_count> buttons{};
  };

  /** @brief Whether @p control reads as pressed in @p state.
   *
   *  Buttons read directly. A stick direction is down when that stick's radial magnitude clears
   *  the dead zone and its component in that direction clears the direction threshold, so a gentle
   *  diagonal registers on both axes. A trigger is down past its half-press point.
   */
  [[nodiscard]] bool is_down(const GamepadState &state, GamepadControl control) noexcept;

  /** @brief The device family a physical input belongs to. */
  enum class InputDevice : std::uint8_t {
    Keyboard,
    Mouse,
    Gamepad,
  };

  /** @brief One physical control: a key, a mouse button, or a gamepad control. */
  struct PhysicalInput {
    /** @brief A Key, MouseButton or GamepadControl value, read according to @c device. */
    std::uint16_t code{};

    InputDevice device{};

    friend bool operator==(const PhysicalInput &, const PhysicalInput &) = default;
  };

  [[nodiscard]] constexpr PhysicalInput physical(Key key) noexcept {
    return {.code = static_cast<std::uint16_t>(key), .device = InputDevice::Keyboard};
  }

  [[nodiscard]] constexpr PhysicalInput physical(MouseButton button) noexcept {
    return {.code = static_cast<std::uint16_t>(button), .device = InputDevice::Mouse};
  }

  [[nodiscard]] constexpr PhysicalInput physical(GamepadControl control) noexcept {
    return {.code = static_cast<std::uint16_t>(control), .device = InputDevice::Gamepad};
  }

  /** @brief Stable JSON spelling of @p device: "keyboard", "mouse" or "gamepad". */
  [[nodiscard]] std::string_view device_name(InputDevice device) noexcept;

  /** @brief Inverse of device_name(); nullopt for any other string. */
  [[nodiscard]] std::optional<InputDevice> parse_device(std::string_view name) noexcept;

  /** @brief Stable JSON name of @p input: its enumerator spelling ("W", "F5", "Left", "LeftStickUp").
   *
   *  @return An empty view when @p input's code names no enumerator of its device.
   */
  [[nodiscard]] std::string_view name_of(PhysicalInput input) noexcept;

  /** @brief Inverse of name_of(), scoped to @p device ("A" is Key::A on the keyboard, the A button on a gamepad).
   *
   *  @return nullopt when @p name is not an enumerator of @p device.
   */
  [[nodiscard]] std::optional<PhysicalInput> parse_physical_input(InputDevice device, std::string_view name) noexcept;

} // namespace corundum::input
