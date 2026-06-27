#pragma once
#include <corundum/audio/audio_backend.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/platform/window.hpp>

#include <expected>
#include <memory>
#include <string>
#include <string_view>

namespace corundum::platform {

  /** @brief Opaque result of create_platform().
   *
   * Holds a platform-specific Window, Renderer and AudioBackend behind the
   * abstract interfaces so callers never need to include platform internals.
   */
  struct PlatformContext {
    std::unique_ptr<Window> window;
    std::unique_ptr<Renderer> renderer;
    std::unique_ptr<corundum::audio::AudioBackend> audio_backend;
  };

  /** @brief Create a platform-specific Window, Renderer and AudioBackend.
   *
   * On macOS this creates a GLFW window with a Metal layer and a sokol
   * Metal renderer. On other platforms the GLFW/GL or GLFW/D3D backend is used.
   * The audio backend wraps sokol_audio (44100 Hz stereo stream callback).
   *
   * @param[in] width  Initial window width in pixels.
   * @param[in] height Initial window height in pixels.
   * @param[in] title  Window title.
   * @return PlatformContext on success, or std::unexpected with an error message.
   * @post Window, renderer, and audio_backend are all valid (non-null) on success.
   */
  [[nodiscard]] std::expected<PlatformContext, std::string> create_platform(unsigned width, unsigned height,
                                                                            std::string_view title);

} // namespace corundum::platform
