// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/audio/audio_backend.hpp>
#include <corundum/platform/gpu_context.hpp>
#include <corundum/platform/handle.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/platform/window.hpp>

#include <expected>
#include <memory>
#include <string>
#include <string_view>

namespace corundum::platform {

  /** @brief Opaque result of create_platform().
   *
   * Holds a platform-specific Window, GpuContext, Renderer and AudioBackend
   * behind the abstract interfaces so callers never need to include platform
   * internals.
   *
   * Fields are declared in destruction order: members are destroyed in reverse,
   * so the renderer is released while the GPU device is still alive, and the GPU
   * device while its window is still alive. Do not reorder.
   */
  struct PlatformContext {
    Handle<Window> window;

    Handle<GpuContext> gpu;

    Handle<Renderer> renderer;

    std::unique_ptr<corundum::audio::AudioBackend> audio_backend;
  };

  /** @brief Create a platform-specific Window only (no renderer or audio).
   *
   * The window is fully initialised and ready for GPU context creation.
   *
   * @param[in] width   Initial window width in pixels.
   * @param[in] height  Initial window height in pixels.
   * @return Owning pointer to the Window, or std::unexpected with an error message.
   */
  [[nodiscard]] std::expected<std::unique_ptr<Window>, std::string> create_window(unsigned width, unsigned height,
                                                                                  std::string_view title);

  /** @brief Create a platform-specific Window, GPU context, Renderer and AudioBackend.
   *
   * @param[in] width   Initial window width in pixels.
   * @param[in] height  Initial window height in pixels.
   * @return The bundle on success, or std::unexpected with an error message.
   * @post Window, gpu, renderer, and audio_backend are all valid (non-null) on success.
   */
  [[nodiscard]] std::expected<PlatformContext, std::string> create_platform(unsigned width, unsigned height,
                                                                            std::string_view title);

} // namespace corundum::platform
