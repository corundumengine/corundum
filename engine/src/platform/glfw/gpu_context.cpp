// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include "core/warn_log.hpp"
#include "glfw_window.hpp"
#include "gpu_backend.hpp"
#include <corundum/core/render_resolution.hpp>
#include <corundum/platform/gpu_context.hpp>

#include <sokol_gfx.h>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdint>
#include <format>

namespace corundum::platform {

  namespace gpu_backend = glfw::gpu_backend;

  namespace {

    /// sokol_gfx's device is process-global: sg_setup() asserts when one is already live and
    /// sg_shutdown() tears the device down for the whole process. Track ownership so a second
    /// context is rejected instead of asserting.
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables): TU-local, must stay mutable.
    bool s_gpu_device_live = false;

    /// sokol log levels: 0 = panic, 1 = error, 2 = warn, 3 = info, 4 = debug. Surface failures
    /// only; routine warnings would be noise on the game's stderr.
    constexpr uint32_t k_max_reported_log_level = 1;

    /// Supported range for the internal render scale, so a caller cannot ask for a zero-size target
    /// or an absurd supersample. The upper bound is generous enough for a 3x panel.
    constexpr float k_min_render_scale = 0.125f;
    constexpr float k_max_render_scale = 4.f;

  } // namespace

  struct GpuContext::Impl {
    GLFWwindow *window{nullptr};

    /// Backend device/swapchain seam for this context; null when the backend needs no separate state.
    gpu_backend::Context *backend{nullptr};

    float render_scale{1.f};

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
    // Default to native resolution: tools drive begin_default_pass() directly and keep it. The game
    // renderer lowers itself to the logical resolution (SokolRenderer's constructor).
    ctx->impl_->render_scale = ctx->dpi_scale();

    auto backend = gpu_backend::create(raw);
    if (!backend)
      return std::unexpected(std::format("GpuContext::create: {}", backend.error()));
    ctx->impl_->backend = *backend;

    sg_desc sdesc{};
    if (auto configured = gpu_backend::configure_sokol(sdesc, ctx->impl_->backend); !configured)
      return std::unexpected(std::format("GpuContext::create: {}", configured.error()));
    sdesc.logger.func = [](const char *tag, uint32_t level, uint32_t item_id, const char *msg, uint32_t line,
                           const char *file, void *) {
      if (level <= k_max_reported_log_level)
        corundum::detail::warn_log("[{}] item={} ({}:{}) {}", tag ? tag : "sg", item_id, file ? file : "?", line,
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
    gpu_backend::destroy(impl_->backend);
  }

  bool GpuContext::begin_default_pass(core::math::Colour clear) {
    const auto [fb_w, fb_h] = render_size();
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

    if (!gpu_backend::begin_pass(swapchain, impl_->backend, fb_w, fb_h)) {
      impl_->pass_active = false;
      return false;
    }

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
    gpu_backend::end_pass(impl_->backend, impl_->window);
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

  void GpuContext::set_render_scale(float scale) noexcept {
    impl_->render_scale = std::clamp(scale, k_min_render_scale, k_max_render_scale);
  }

  std::pair<int, int> GpuContext::render_size() const noexcept {
    const auto [win_w, win_h] = window_size();
    const auto [fb_w, fb_h] = framebuffer_size();
    const core::RenderResolution resolution =
        core::compute_render_resolution(win_w, win_h, fb_w, fb_h, impl_->render_scale);
    return {resolution.width, resolution.height};
  }

  float GpuContext::dpi_scale() const noexcept {
    float xscale{};
    float yscale{};
    glfwGetWindowContentScale(impl_->window, &xscale, &yscale);
    return xscale > 0.f ? xscale : 1.f;
  }

} // namespace corundum::platform
