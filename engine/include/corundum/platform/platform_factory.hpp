#pragma once
#include <corundum/platform/renderer.hpp>
#include <corundum/platform/window.hpp>

#include <expected>
#include <memory>
#include <string>
#include <string_view>

namespace corundum::platform {

  /** @brief Opaque result of create_platform().
   *
   * Holds a platform-specific Window and Renderer behind the abstract
   * interfaces so callers never need to include platform internals.
   */
  struct PlatformContext {
    std::unique_ptr<Window> window;
    std::unique_ptr<Renderer> renderer;
  };

  /** @brief Create a platform-specific Window and Renderer pair.
   *
   * On macOS this creates a GLFW window with a Metal layer and a sokol
   * Metal renderer. On other platforms the GLFW/GL or GLFW/D3D backend is used.
   *
   * @param[in] width  Initial window width in pixels.
   * @param[in] height Initial window height in pixels.
   * @param[in] title  Window title.
   * @return PlatformContext on success, or std::unexpected with an error message.
   * @post Both window and renderer are valid (non-null) on success.
   */
  [[nodiscard]] std::expected<PlatformContext, std::string>
  create_platform(unsigned width = 1280, unsigned height = 720, std::string_view title = "Project Keystone");

} // namespace corundum::platform
