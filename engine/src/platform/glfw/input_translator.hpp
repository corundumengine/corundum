// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/input/physical_input.hpp>

#include <optional>

struct GLFWgamepadstate;

namespace corundum::platform::glfw {

  /** @brief The engine key for a GLFW key token; nullopt for GLFW_KEY_UNKNOWN. */
  [[nodiscard]] std::optional<corundum::input::Key> to_key(int glfw_key) noexcept;

  /** @brief The engine mouse button for a GLFW button; nullopt for buttons the engine does not name. */
  [[nodiscard]] std::optional<corundum::input::MouseButton> to_mouse_button(int glfw_button) noexcept;

  /** @brief Copy a GLFW gamepad snapshot into the engine's device-neutral form. */
  [[nodiscard]] corundum::input::GamepadState to_gamepad_state(const GLFWgamepadstate &state) noexcept;

} // namespace corundum::platform::glfw
