// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

// Metal implementation of the GPU context's graphics seam. Compiled only on Apple
// (engine/src/platform/CMakeLists.txt).

#include "gpu_backend.hpp"

#include "glfw_window_metal.h"

#include <GLFW/glfw3.h>
#include <sokol_gfx.h>

#include <memory>
#include <new>
#include <string>
#include <utility>

namespace corundum::platform::glfw::gpu_backend {

  struct Context {
    MetalLayer *layer{nullptr};
    const void *drawable{nullptr};
  };

  std::expected<Context *, std::string> create(GLFWwindow *win) {
    // Borrowed wrapper: the window owns the layer, so this handle only needs its wrapper freed.
    MetalLayer *layer = metal_get_layer(win);
    if (layer == nullptr)
      return std::unexpected("failed to get Metal layer");
    std::unique_ptr<Context> context{new (std::nothrow) Context{}};
    if (context == nullptr) {
      metal_teardown_layer(layer);
      return std::unexpected("failed to allocate Metal context state");
    }
    context->layer = layer;
    return context.release();
  }

  std::expected<void, std::string> configure_sokol(sg_desc &desc, const Context *context) {
    desc.environment.metal.device = metal_device(context->layer);
    if (desc.environment.metal.device == nullptr)
      return std::unexpected("Metal layer has no device");
    return {};
  }

  bool begin_pass(sg_swapchain &swapchain, Context *context, int width, int height) noexcept {
    // The layer latches drawableSize once it has vended a drawable, so it has to be resized to
    // match the pass dimensions every frame.
    metal_set_drawable_size(context->layer, width, height);
    metal_release_drawable(context->drawable);
    context->drawable = metal_next_drawable(context->layer);
    swapchain.color_format = SG_PIXELFORMAT_BGRA8;
    swapchain.metal.current_drawable = context->drawable;
    return context->drawable != nullptr;
  }

  void end_pass(Context *context, GLFWwindow * /*win*/) noexcept {
    // sg_commit() presents and drops sokol's reference, so the retained drawable is free to go.
    metal_release_drawable(context->drawable);
    context->drawable = nullptr;
  }

  void destroy(Context *context) noexcept {
    const std::unique_ptr<Context> owned{context};
    if (owned == nullptr)
      return;
    metal_release_drawable(owned->drawable);
    metal_teardown_layer(owned->layer);
  }

} // namespace corundum::platform::glfw::gpu_backend
