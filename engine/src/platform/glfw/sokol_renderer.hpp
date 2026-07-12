#pragma once
#include <corundum/platform/renderer.hpp>

#include <memory>

namespace corundum::platform {

  class GpuContext;

} // namespace corundum::platform

namespace corundum::platform::glfw {

  /** @brief Create a sokol_gfx-backed Renderer attached to the given GPU context.
   *
   * The GPU context owns the sokol device and per-frame render pass; the
   * renderer owns shaders, pipelines, vertex buffers, samplers and texture
   * resources.
   *
   * @param[in] gpu_ctx  A fully-initialised GpuContext with an active sokol device.
   *
   * @return Owning pointer to the initialised Renderer.
   */
  [[nodiscard]] std::unique_ptr<corundum::platform::Renderer> make_sokol_renderer(corundum::platform::GpuContext &gpu_ctx);

} // namespace corundum::platform::glfw
