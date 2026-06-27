#pragma once
#include <corundum/platform/renderer.hpp>

#include <memory>

struct GLFWwindow;

namespace corundum::platform::glfw {

  /** @brief Create a sokol_gfx-backed Renderer attached to the given GLFW window.
   *
   * On macOS uses the Metal backend; on other platforms uses the OpenGL Core backend.
   *
   * @param[in] window  An initialised GLFW window with active context or CAMetalLayer.
   *
   * @pre On macOS @p window must already have a CAMetalLayer attached.
   * @pre On non-macOS @p window must have an active OpenGL context.
   *
   * @return Owning pointer to the initialised Renderer.
   */
  [[nodiscard]] std::unique_ptr<corundum::platform::Renderer> make_sokol_renderer(::GLFWwindow *window);

} // namespace corundum::platform::glfw
