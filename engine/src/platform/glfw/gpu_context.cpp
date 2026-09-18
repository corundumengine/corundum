// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include "glfw_window.hpp"
#include <corundum/platform/gpu_context.hpp>

#include <sokol_gfx.h>

#ifdef SOKOL_METAL
#include "glfw_window_metal.h"
#endif

#include <GLFW/glfw3.h>

#include <cstdint>
#include <cstdio>
#include <print>

namespace corundum::platform {

  namespace {

    /// sokol_gfx's device is process-global: sg_setup() asserts when one is already live and
    /// sg_shutdown() tears the device down for the whole process. Track ownership so a second
    /// context is rejected instead of asserting.
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables): TU-local, must stay mutable.
    bool s_gpu_device_live = false;

    /// sokol log levels: 0 = panic, 1 = error, 2 = warn, 3 = info, 4 = debug. Surface failures
    /// only; routine warnings would be noise on the game's stderr.
    constexpr uint32_t k_max_reported_log_level = 1;

  } // namespace

  struct GpuContext::Impl {
    GLFWwindow *window{nullptr};
#ifdef SOKOL_METAL
    MetalLayer *metal_layer{nullptr};
    const void *metal_drawable{nullptr};
#endif
    bool pass_active{false};
  };

  GpuContext::GpuContext() : impl_{std::make_unique<Impl>()} {}

  std::expected<std::unique_ptr<GpuContext>, std::string> GpuContext::create(Window &window) {
    if (s_gpu_device_live)
      return std::unexpected("GpuContext::create: a GPU context already owns the process-wide sokol device");

    const auto *glfw_win = dynamic_cast<glfw::GLFWWindow *>(&window);
    if (glfw_win == nullptr)
      return std::unexpected("GpuContext::create: window is not a GLFW window");

    auto *raw = static_cast<::GLFWwindow *>(glfw_win->native_handle());
    if (raw == nullptr)
      return std::unexpected("GpuContext::create: window has no GLFW handle");

    auto ctx = std::unique_ptr<GpuContext>(new GpuContext());
    ctx->impl_->window = raw;

#ifdef SOKOL_METAL
    // Borrowed wrapper: the window holds its own owning handle to this layer, so this one only
    // needs metal_teardown_layer() to free the wrapper.
    ctx->impl_->metal_layer = metal_get_layer(raw);
    if (ctx->impl_->metal_layer == nullptr)
      return std::unexpected("GpuContext::create: failed to get Metal layer");
#endif

    sg_desc sdesc{};
#ifdef SOKOL_METAL
    sdesc.environment.metal.device = metal_device(ctx->impl_->metal_layer);
    if (sdesc.environment.metal.device == nullptr)
      return std::unexpected("GpuContext::create: Metal layer has no device");
#else
    sdesc.environment.gl.default_framebuffer = 0;
#endif
    sdesc.logger.func = [](const char *tag, uint32_t level, uint32_t item_id, const char *msg, uint32_t line,
                           const char *file, void *) {
      if (level <= k_max_reported_log_level)
        std::println(stderr, "[{}] item={} ({}:{}) {}", tag ? tag : "sg", item_id, file ? file : "?", line,
                     msg ? msg : "");
    };
    sg_setup(&sdesc);
    if (!sg_isvalid())
      return std::unexpected("GpuContext::create: sg_setup failed");

    s_gpu_device_live = true;
    return ctx;
  }

  GpuContext::~GpuContext() {
    if (s_gpu_device_live) {
      s_gpu_device_live = false;
      if (sg_isvalid())
        sg_shutdown();
    }
#ifdef SOKOL_METAL
    metal_release_drawable(impl_->metal_drawable);
    if (impl_->metal_layer != nullptr)
      metal_teardown_layer(impl_->metal_layer);
#endif
  }

  bool GpuContext::begin_default_pass(core::math::Colour clear) {
    int fb_w{};
    int fb_h{};
    glfwGetFramebufferSize(impl_->window, &fb_w, &fb_h);
    if (fb_w <= 0 || fb_h <= 0) {
      // Minimized or otherwise zero-sized window: no render target this frame.
      impl_->pass_active = false;
      return false;
    }

    sg_pass_action action{};
    action.colors[0].load_action = SG_LOADACTION_CLEAR;
    action.colors[0].clear_value = {
        .r = static_cast<float>(clear.r) / 255.f,
        .g = static_cast<float>(clear.g) / 255.f,
        .b = static_cast<float>(clear.b) / 255.f,
        .a = static_cast<float>(clear.a) / 255.f,
    };

    sg_swapchain swapchain{};
    swapchain.width = fb_w;
    swapchain.height = fb_h;
    swapchain.sample_count = 1;
    swapchain.depth_format = SG_PIXELFORMAT_NONE;

#ifdef SOKOL_METAL
    // The layer latches drawableSize once it has vended a drawable, so it has to be resized
    // to match the pass dimensions every frame.
    metal_set_drawable_size(impl_->metal_layer, fb_w, fb_h);
    metal_release_drawable(impl_->metal_drawable);
    impl_->metal_drawable = metal_next_drawable(impl_->metal_layer);
    swapchain.color_format = SG_PIXELFORMAT_BGRA8;
    swapchain.metal.current_drawable = impl_->metal_drawable;
    if (swapchain.metal.current_drawable == nullptr) {
      impl_->pass_active = false;
      return false;
    }
#else
    swapchain.color_format = SG_PIXELFORMAT_RGBA8;
#endif

    sg_pass pass{};
    pass.action = action;
    pass.swapchain = swapchain;
    sg_begin_pass(&pass);
    impl_->pass_active = true;
    return true;
  }

  void GpuContext::end_frame() {
    if (!impl_->pass_active)
      return;
    impl_->pass_active = false;
    sg_end_pass();
    sg_commit();
#ifdef SOKOL_METAL
    // sg_commit() presents and drops sokol's reference, so the retained drawable is free to go.
    metal_release_drawable(impl_->metal_drawable);
    impl_->metal_drawable = nullptr;
#else
    glfwSwapBuffers(impl_->window);
#endif
  }

  std::pair<int, int> GpuContext::window_size() const noexcept {
    int w{};
    int h{};
    glfwGetWindowSize(impl_->window, &w, &h);
    return {w, h};
  }

  std::pair<int, int> GpuContext::framebuffer_size() const noexcept {
    int w{};
    int h{};
    glfwGetFramebufferSize(impl_->window, &w, &h);
    return {w, h};
  }

  float GpuContext::dpi_scale() const noexcept {
    float xscale{};
    float yscale{};
    glfwGetWindowContentScale(impl_->window, &xscale, &yscale);
    return xscale > 0.f ? xscale : 1.f;
  }

} // namespace corundum::platform
