#include <corundum/platform/platform_factory.hpp>

#include "glfw_window.hpp"
#include "sokol_renderer.hpp"

namespace corundum::platform {

  std::expected<PlatformContext, std::string> create_platform(unsigned width, unsigned height, std::string_view title) {
    auto window_result = glfw::GLFWWindow::create(width, height, title);
    if (!window_result)
      return std::unexpected("Failed to create GLFW window");

    auto window_ptr = std::move(*window_result);
    auto renderer = std::make_unique<Renderer>(glfw::make_sokol_renderer(window_ptr->glfw_window()));

    return PlatformContext{
        .window = std::move(window_ptr),
        .renderer = std::move(renderer),
    };
  }

} // namespace corundum::platform
