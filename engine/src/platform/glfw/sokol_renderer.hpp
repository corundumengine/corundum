#pragma once
#include <corundum/platform/renderer.hpp>

struct GLFWwindow;

namespace corundum::platform::glfw {

  /** @brief Create a sokol_gfx-backed Renderer attached to the given GLFW window.
   *
   * On macOS uses the Metal backend; on other platforms uses the OpenGL Core backend.
   * The returned Renderer's state pointer owns a heap-allocated internal implementation.
   * Call Renderer::destroy() to shut down sokol and free resources.
   *
   * @pre On macOS @p window must already have a CAMetalLayer attached.
   * @pre On non-macOS @p window must have an active OpenGL context.
   */
  [[nodiscard]] corundum::platform::Renderer make_sokol_renderer(::GLFWwindow *window);

} // namespace corundum::platform::glfw
