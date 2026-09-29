// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// The window's graphics-API integration: the one seam that differs between graphics backends. The
// shared window code calls only these; exactly one backend translation unit compiles and links per
// build, selected in engine/src/platform/CMakeLists.txt, so glfw_window.cpp carries no graphics-API
// conditional. See AGENTS.md "Architecture".

struct GLFWwindow;

namespace corundum::platform::glfw::window_graphics {

  /** @brief Per-window graphics state, defined separately by each backend's translation unit.
   *
   * A backend that needs per-window GPU state owns it here; one whose context already lives on the
   * GLFW window uses a null handle. The type is incomplete on purpose: the shared window code only
   * stores and forwards the pointer.
   */
  struct Context;

  /** @brief Whether this backend's platform can keep a window hidden and still present a frame.
   *
   * The window starts hidden when this is true and visible otherwise: a platform that cannot present
   * a hidden window would never reach the first frame the reveal waits for. The answer is a property
   * of the platform each backend builds for, so it lives with that backend.
   */
  [[nodiscard]] bool can_present_hidden() noexcept;

  /** @brief Set the window-creation hints that differ by graphics backend.
   *
   * @pre glfwDefaultWindowHints() has been called and no window exists yet.
   * @post The client API and any context version/profile hints the backend needs are set.
   */
  void apply_creation_hints() noexcept;

  /** @brief Create the graphics context for @p win.
   *
   * @pre @p win is a valid GLFW window.
   * @return The context, or nullptr on failure or when the backend needs none.
   */
  [[nodiscard]] Context *create(GLFWwindow *win) noexcept;

  /** @brief Match @p context to @p win's current content scale; a no-op when the backend has none. */
  void sync_content_scale(const Context *context, GLFWwindow *win) noexcept;

  /** @brief Apply display sync to @p context, or to @p win where the backend has no separate context. */
  void set_vsync(const Context *context, GLFWwindow *win, bool enabled) noexcept;

  /** @brief Destroy @p context; no-op if null. @pre @p win is still alive. */
  void destroy(Context *context) noexcept;

} // namespace corundum::platform::glfw::window_graphics
