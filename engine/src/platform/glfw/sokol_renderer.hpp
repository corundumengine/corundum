// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/platform/renderer.hpp>

#include <memory>

namespace corundum::platform {

class GpuContext;

} // namespace corundum::platform

namespace corundum::core {
class ContentSource;
} // namespace corundum::core

namespace corundum::platform::glfw {

/** Create a sokol_gfx-backed Renderer attached to the given GPU context.
 *
 *  The GPU context owns the sokol device and per-frame render pass; the
 *  renderer owns shaders, pipelines, vertex buffers, samplers and texture
 *  resources.
 *
 *  @param gpu_ctx  A fully-initialised GpuContext with an active sokol device.
 *  @param content  Source the renderer reads textures and fonts through. Must outlive the renderer.
 *
 *  @return Owning pointer to the initialised Renderer.
 *
 *  @note Shader sources and descriptor bindings are per-backend, provided by the translation
 *        unit selected for the build.
 */
[[nodiscard]] std::unique_ptr<Renderer> make_sokol_renderer(GpuContext &gpu_ctx, const core::ContentSource &content);

} // namespace corundum::platform::glfw
