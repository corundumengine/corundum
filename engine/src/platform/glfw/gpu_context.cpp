#include "glfw_window.hpp"
#include <corundum/platform/gpu_context.hpp>

#include <sokol_gfx.h>

#ifdef SOKOL_METAL
#include "glfw_window_metal.h"
#endif

#include <GLFW/glfw3.h>

#include <print>

namespace corundum::platform {

  struct GpuContext::Impl {
    GLFWwindow *window{nullptr};
#ifdef SOKOL_METAL
    void *metal_layer{nullptr};
#endif
    bool pass_active{false};
  };

  GpuContext::GpuContext() : impl_{std::make_unique<Impl>()} {}

  std::expected<std::unique_ptr<GpuContext>, std::string> GpuContext::create(Window &window) {
    auto &glfw_win = static_cast<glfw::GLFWWindow &>(window);
    ::GLFWwindow *raw = glfw_win.glfw_window();
    if (!raw)
      return std::unexpected("GpuContext::create: window has no GLFW handle");

    auto ctx = std::unique_ptr<GpuContext>(new GpuContext());
    ctx->impl_->window = raw;

#ifdef SOKOL_METAL
    ctx->impl_->metal_layer = metal_get_layer(raw);
#endif

    sg_desc sdesc{};
#ifdef SOKOL_METAL
    sdesc.environment.metal.device = metal_device(ctx->impl_->metal_layer);
#else
    sdesc.environment.gl.default_framebuffer = 0;
#endif
    sdesc.logger.func = [](const char *tag, uint32_t level, uint32_t item_id, const char *msg, uint32_t line,
                           const char *file, void *) {
      if (level < 2)
        std::println(stderr, "[{}] item={} ({}:{}) {}", tag ? tag : "sg", item_id, file ? file : "?", line,
                     msg ? msg : "");
    };
    sg_setup(&sdesc);

    return ctx;
  }

  GpuContext::~GpuContext() {
    if (sg_isvalid())
      sg_shutdown();
  }

  bool GpuContext::begin_default_pass(core::math::Colour clear) {
    int fb_w{}, fb_h{};
    glfwGetFramebufferSize(impl_->window, &fb_w, &fb_h);

    sg_pass_action action{};
    action.colors[0].load_action = SG_LOADACTION_CLEAR;
    action.colors[0].clear_value = {
        clear.r / 255.f,
        clear.g / 255.f,
        clear.b / 255.f,
        clear.a / 255.f,
    };

    sg_swapchain swapchain{};
    swapchain.width = fb_w;
    swapchain.height = fb_h;
    swapchain.sample_count = 1;
    swapchain.depth_format = SG_PIXELFORMAT_NONE;

#ifdef SOKOL_METAL
    swapchain.color_format = SG_PIXELFORMAT_BGRA8;
    swapchain.metal.current_drawable = metal_next_drawable(impl_->metal_layer);
    if (!swapchain.metal.current_drawable) {
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
#ifndef SOKOL_METAL
    glfwSwapBuffers(impl_->window);
#endif
  }

  std::pair<int, int> GpuContext::window_size() const noexcept {
    int w{}, h{};
    glfwGetWindowSize(impl_->window, &w, &h);
    return {w, h};
  }

  std::pair<int, int> GpuContext::framebuffer_size() const noexcept {
    int w{}, h{};
    glfwGetFramebufferSize(impl_->window, &w, &h);
    return {w, h};
  }

  float GpuContext::dpi_scale() const noexcept {
    float xscale{}, yscale{};
    glfwGetWindowContentScale(impl_->window, &xscale, &yscale);
    return xscale;
  }

} // namespace corundum::platform
