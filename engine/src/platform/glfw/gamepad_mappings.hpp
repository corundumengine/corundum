// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <span>
#include <string>
#include <string_view>

namespace corundum::platform::glfw {

  /** @brief SDL_GameControllerDB rows for the build platform, one mapping per element.
   *
   *  Embedded at configure time from the pinned database (cmake/Dependencies.cmake), so loading
   *  them opens no file.
   */
  [[nodiscard]] std::span<const std::string_view> gamepad_mappings() noexcept;

  /** @brief @p mappings joined into the single newline-separated string
   *  glfwUpdateGamepadMappings() expects.
   *
   *  GLFW-free so the joining contract is testable without linking the backend. */
  [[nodiscard]] inline std::string join_gamepad_mappings(std::span<const std::string_view> mappings) {
    std::string joined;
    for (const std::string_view mapping : mappings) {
      joined += mapping;
      joined += '\n';
    }
    return joined;
  }

} // namespace corundum::platform::glfw
