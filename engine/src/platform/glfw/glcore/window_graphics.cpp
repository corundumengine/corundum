// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

// GLCore implementation of the GLFW window's graphics seam. Compiled on every non-Apple platform
// (engine/src/platform/CMakeLists.txt).

#include "window_graphics.hpp"

#include <GLFW/glfw3.h>

namespace corundum::platform::glfw::window_graphics {

  bool can_present_hidden() noexcept {
    // GLFW withholds the first buffer on Wayland until the compositor maps the surface, and a
    // hidden window is never mapped, so the reveal-after-first-frame flow would never complete.
    return glfwGetPlatform() != GLFW_PLATFORM_WAYLAND;
  }

  void apply_creation_hints() noexcept {
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  }

  Context *create(GLFWwindow *win) noexcept {
    glfwMakeContextCurrent(win);
    // The GL context lives on the GLFW window; there is no separate object to own.
    return nullptr;
  }

  void sync_content_scale(const Context * /*context*/, GLFWwindow * /*win*/) noexcept {}

  void set_vsync(const Context * /*context*/, GLFWwindow *win, bool enabled) noexcept {
    glfwMakeContextCurrent(win);
    glfwSwapInterval(enabled ? 1 : 0);
  }

  void destroy(Context * /*context*/) noexcept {}

} // namespace corundum::platform::glfw::window_graphics
