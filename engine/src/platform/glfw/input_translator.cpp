// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include "input_translator.hpp"

#include <corundum/input/physical_input.hpp>

#include <GLFW/glfw3.h>

#include <cstddef>
#include <optional>

namespace corundum::platform::glfw {

  namespace {
    using corundum::input::GamepadControl;
    using corundum::input::Key;
    using corundum::input::MouseButton;

    // The engine's physical-input enums mirror GLFW's values, so conversion is a cast; these
    // pin the ends of every range so a GLFW renumbering fails the build instead of misrouting keys.
    static_assert(static_cast<int>(Key::Space) == GLFW_KEY_SPACE);
    static_assert(static_cast<int>(Key::Z) == GLFW_KEY_Z);
    static_assert(static_cast<int>(Key::GraveAccent) == GLFW_KEY_GRAVE_ACCENT);
    static_assert(static_cast<int>(Key::World2) == GLFW_KEY_WORLD_2);
    static_assert(static_cast<int>(Key::End) == GLFW_KEY_END);
    static_assert(static_cast<int>(Key::Pause) == GLFW_KEY_PAUSE);
    static_assert(static_cast<int>(Key::F25) == GLFW_KEY_F25);
    static_assert(static_cast<int>(Key::KeypadEqual) == GLFW_KEY_KP_EQUAL);
    static_assert(static_cast<int>(Key::Menu) == GLFW_KEY_LAST);
    static_assert(static_cast<int>(MouseButton::Middle) == GLFW_MOUSE_BUTTON_MIDDLE);
    static_assert(static_cast<int>(GamepadControl::DpadLeft) == GLFW_GAMEPAD_BUTTON_LAST);
    static_assert(corundum::input::k_gamepad_button_count == GLFW_GAMEPAD_BUTTON_LAST + 1);
    static_assert(corundum::input::k_gamepad_axis_count == GLFW_GAMEPAD_AXIS_LAST + 1);
  } // namespace

  std::optional<corundum::input::Key> to_key(int glfw_key) noexcept {
    if (glfw_key == GLFW_KEY_UNKNOWN)
      return std::nullopt;
    // Every other GLFW key token has an enumerator of equal value.
    return static_cast<Key>(glfw_key);
  }

  std::optional<corundum::input::MouseButton> to_mouse_button(int glfw_button) noexcept {
    if (glfw_button < GLFW_MOUSE_BUTTON_LEFT || glfw_button > GLFW_MOUSE_BUTTON_MIDDLE)
      return std::nullopt;
    return static_cast<MouseButton>(glfw_button);
  }

  corundum::input::GamepadState to_gamepad_state(const GLFWgamepadstate &state) noexcept {
    corundum::input::GamepadState result{};
    for (std::size_t i = 0; i < result.buttons.size(); ++i)
      result.buttons[i] = state.buttons[i] == GLFW_PRESS;
    for (std::size_t i = 0; i < result.axes.size(); ++i)
      result.axes[i] = state.axes[i];
    return result;
  }

} // namespace corundum::platform::glfw
