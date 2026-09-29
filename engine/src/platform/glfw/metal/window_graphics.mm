// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

// Metal implementation of the GLFW window's graphics seam. Compiled only on Apple
// (engine/src/platform/CMakeLists.txt).

#include "window_graphics.hpp"

#include "glfw_window_metal.h"

#include <GLFW/glfw3.h>

#include <memory>
#include <new>

namespace corundum::platform::glfw::window_graphics {

  struct Context {
    MetalLayer *layer{nullptr};
  };

  bool can_present_hidden() noexcept {
    // Cocoa presents a window before it is shown, so the reveal-after-first-frame flow works.
    return true;
  }

  void apply_creation_hints() noexcept {
    // Metal renders into a CAMetalLayer, so GLFW must not create a GL context.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  }

  Context *create(GLFWwindow *win) noexcept {
    MetalLayer *layer = metal_setup_layer(win);
    if (layer == nullptr)
      return nullptr;
    std::unique_ptr<Context> context{new (std::nothrow) Context{}};
    if (context == nullptr) {
      metal_teardown_layer(layer);
      return nullptr;
    }
    context->layer = layer;
    return context.release();
  }

  void sync_content_scale(const Context *context, GLFWwindow *win) noexcept {
    if (context != nullptr)
      metal_sync_contents_scale(context->layer, win);
  }

  void set_vsync(const Context *context, GLFWwindow * /*win*/, bool enabled) noexcept {
    if (context != nullptr)
      metal_set_display_sync(context->layer, enabled ? 1 : 0);
  }

  // NOLINTNEXTLINE(misc-const-correctness): consumes @p context and frees it.
  void destroy(Context *context) noexcept {
    const std::unique_ptr<Context> owned{context};
    if (owned != nullptr)
      metal_teardown_layer(owned->layer);
  }

} // namespace corundum::platform::glfw::window_graphics
