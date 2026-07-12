#include <corundum/platform/platform_factory.hpp>

#include "../sokol/sokol_audio_backend.hpp"
#include "glfw_window.hpp"
#include "sokol_renderer.hpp"

namespace corundum::platform {

  std::expected<std::unique_ptr<Window>, std::string> create_window(unsigned w, unsigned h, std::string_view title) {
    auto result = glfw::GLFWWindow::create(w, h, title);
    if (!result)
      return std::unexpected("Failed to create GLFW window");
    return std::move(*result);
  }

  std::expected<PlatformContext, std::string> create_platform(unsigned width, unsigned height, std::string_view title) {
    auto window_result = glfw::GLFWWindow::create(width, height, title);
    if (!window_result)
      return std::unexpected("Failed to create GLFW window");

    auto window_ptr = std::move(*window_result);

    auto gpu_result = GpuContext::create(*window_ptr);
    if (!gpu_result)
      return std::unexpected("Failed to create GPU context");

    auto renderer = glfw::make_sokol_renderer(**gpu_result);
    auto audio = glfw::make_sokol_audio_backend();

    return PlatformContext{
        .window = std::move(window_ptr),
        .gpu = std::move(*gpu_result),
        .renderer = std::move(renderer),
        .audio_backend = std::move(audio),
    };
  }

} // namespace corundum::platform
