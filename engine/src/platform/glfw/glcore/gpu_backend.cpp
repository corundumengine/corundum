// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

// GLCore implementation of the GPU context's graphics seam. Compiled on every non-Apple platform
// (engine/src/platform/CMakeLists.txt).

#include "gpu_backend.hpp"

#include <GLFW/glfw3.h>
#include <sokol_gfx.h>

#include <string>

namespace corundum::platform::glfw::gpu_backend {

  std::expected<Context *, std::string> create(GLFWwindow * /*win*/) {
    // The GL backend has no separate device or drawable state; sokol drives the default
    // framebuffer directly.
    return nullptr;
  }

  std::expected<void, std::string> configure_sokol(sg_desc & /*desc*/, const Context * /*context*/) {
    return {};
  }

  bool begin_pass(sg_swapchain &swapchain, Context * /*context*/, int /*width*/, int /*height*/) noexcept {
    swapchain.color_format = SG_PIXELFORMAT_RGBA8;
    return true;
  }

  void end_pass(Context * /*context*/, GLFWwindow *win) noexcept {
    glfwSwapBuffers(win);
  }

  void destroy(Context * /*context*/) noexcept {}

} // namespace corundum::platform::glfw::gpu_backend
