// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/platform/platform_factory.hpp>
#include <expected>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "../sokol/sokol_audio_backend.hpp"
#include "glfw_window.hpp"
#include "sokol_renderer.hpp"
#include <corundum/platform/handle.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/platform/window.hpp>

namespace corundum::platform {

  namespace {

    // Destruction functions live here (the backend TU). They give Engine's default-
    // constructible handles a runtime-supplied deleter and keep the delete-expression in
    // this TU; the actual virtual dispatch still happens through the interface destructors.
    template <typename T> void destroy(T *object) noexcept {
      std::default_delete<T>{}(object);
    }

  } // namespace

  std::expected<std::unique_ptr<Window>, std::string> create_window(unsigned width, unsigned height,
                                                                    std::string_view title) {
    auto result = glfw::GLFWWindow::create(width, height, title);
    if (!result)
      return std::unexpected(std::format("Failed to create GLFW window: {}", glfw::to_string(result.error())));
    return std::move(*result);
  }

  std::expected<PlatformContext, std::string> create_platform(unsigned width, unsigned height, std::string_view title) {
    auto window = create_window(width, height, title);
    if (!window)
      return std::unexpected(window.error());

    auto gpu_result = GpuContext::create(**window);
    if (!gpu_result)
      return std::unexpected(std::format("Failed to create GPU context: {}", gpu_result.error()));
    auto gpu_ptr = std::move(*gpu_result);

    auto renderer = glfw::make_sokol_renderer(*gpu_ptr);
    auto audio = sokol::make_sokol_audio_backend();

    return PlatformContext{
        .window = Handle<Window>{window->release(), BackendDeleter<Window>{&destroy<Window>}},
        .gpu = Handle<GpuContext>{gpu_ptr.release(), BackendDeleter<GpuContext>{&destroy<GpuContext>}},
        .renderer = Handle<Renderer>{renderer.release(), BackendDeleter<Renderer>{&destroy<Renderer>}},
        .audio_backend = std::move(audio),
    };
  }

} // namespace corundum::platform
