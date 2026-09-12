// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/platform/platform_factory.hpp>
#include <expected>
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

    // Destruction functions live here (the backend TU) so the handles Engine holds
    // can delete their objects without the engine core referencing these destructors.
    void destroy_window(Window *window) noexcept {
      std::default_delete<Window>{}(window);
    }

    void destroy_gpu_context(GpuContext *gpu) noexcept {
      std::default_delete<GpuContext>{}(gpu);
    }

    void destroy_renderer(Renderer *renderer) noexcept {
      std::default_delete<Renderer>{}(renderer);
    }

  } // namespace

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
    auto gpu_ptr = std::move(*gpu_result);

    auto renderer = glfw::make_sokol_renderer(*gpu_ptr);
    auto audio = sokol::make_sokol_audio_backend();

    return PlatformContext{
        .window = Handle<Window>{window_ptr.release(), BackendDeleter<Window>{&destroy_window}},
        .gpu = Handle<GpuContext>{gpu_ptr.release(), BackendDeleter<GpuContext>{&destroy_gpu_context}},
        .renderer = Handle<Renderer>{renderer.release(), BackendDeleter<Renderer>{&destroy_renderer}},
        .audio_backend = std::move(audio),
    };
  }

} // namespace corundum::platform
