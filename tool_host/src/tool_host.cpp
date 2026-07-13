#include <corundum/tool_host/tool_host.hpp>

#include <corundum/platform/gpu_context.hpp>
#include <corundum/platform/platform_factory.hpp>

#include <GLFW/glfw3.h>
#include <imgui_impl_glfw.h>
#include <sokol_gfx.h>
#include <sokol_imgui.h>

#include <chrono>
#include <vector>

using namespace corundum::platform;

namespace corundum::tool_host {

  struct ToolHost::Impl {
    std::unique_ptr<Window> window_;
    std::unique_ptr<GpuContext> gpu_ctx_;
    std::unique_ptr<TextureCache> textures_;
    GLFWwindow *glfw_win_{nullptr};
    bool should_close_{false};
  };

  ToolHost::ToolHost() : impl_{std::make_unique<Impl>()} {}

  ToolHost::~ToolHost() {
    ImGui_ImplGlfw_Shutdown();
    simgui_shutdown();
  }

  std::expected<std::unique_ptr<ToolHost>, std::string> ToolHost::create(const ToolHostDesc &desc) {
    auto ctx = std::unique_ptr<ToolHost>(new ToolHost());

    auto window_result =
        platform::create_window(static_cast<unsigned>(desc.width), static_cast<unsigned>(desc.height), desc.title);
    if (!window_result)
      return std::unexpected(window_result.error());
    ctx->impl_->window_ = std::move(*window_result);

    auto gpu_result = GpuContext::create(*ctx->impl_->window_);
    if (!gpu_result)
      return std::unexpected(gpu_result.error());
    ctx->impl_->gpu_ctx_ = std::move(*gpu_result);

    ctx->impl_->textures_ = std::make_unique<TextureCache>(*ctx->impl_->gpu_ctx_);

    ctx->impl_->glfw_win_ = static_cast<GLFWwindow *>(ctx->impl_->window_->native_handle());
    if (!ctx->impl_->glfw_win_)
      return std::unexpected("ToolHost: window has no GLFW handle");

    simgui_setup(simgui_desc_t{
        .depth_format = SG_PIXELFORMAT_NONE,
    });

#ifdef SOKOL_METAL
    ImGui_ImplGlfw_InitForOther(ctx->impl_->glfw_win_, true);
#else
    ImGui_ImplGlfw_InitForOpenGL(ctx->impl_->glfw_win_, true);
#endif

    return ctx;
  }

  void ToolHost::run(std::function<void()> frame_fn) {
    using clock = std::chrono::steady_clock;
    auto prev = clock::now();

    while (!impl_->should_close_ && !glfwWindowShouldClose(impl_->glfw_win_)) {
      glfwPollEvents();

      auto now = clock::now();
      const float dt = std::chrono::duration<float>(now - prev).count();
      prev = now;

      auto [fb_w, fb_h] = impl_->gpu_ctx_->framebuffer_size();
      if (fb_w == 0 || fb_h == 0)
        continue;

      auto [win_w, win_h] = impl_->gpu_ctx_->window_size();
      const float dpi = win_w > 0 ? static_cast<float>(fb_w) / static_cast<float>(win_w) : 1.f;

      ImGui_ImplGlfw_NewFrame();
      simgui_new_frame(simgui_frame_desc_t{
          .width = fb_w,
          .height = fb_h,
          .delta_time = dt,
          .dpi_scale = dpi,
      });

      frame_fn();

      if (!impl_->gpu_ctx_->begin_default_pass({30, 30, 35, 255})) {
        ImGui::EndFrame();
        continue;
      }

      simgui_render();
      impl_->gpu_ctx_->end_frame();
    }
  }

  void ToolHost::request_close() {
    impl_->should_close_ = true;
  }

  void ToolHost::set_title(std::string_view title) {
    glfwSetWindowTitle(impl_->glfw_win_, std::string{title}.c_str());
  }

  Window &ToolHost::window() {
    return *impl_->window_;
  }

  TextureCache &ToolHost::textures() {
    return *impl_->textures_;
  }

  ImTextureID ToolHost::imgui_id(uint32_t texture_id) {
    auto bt = impl_->textures_->backend_handle(texture_id);
    return simgui_imtextureid_with_sampler(sg_view{static_cast<uint32_t>(bt.view)},
                                           sg_sampler{static_cast<uint32_t>(bt.sampler)});
  }

  TextureInfo ToolHost::make_checkerboard(unsigned w, unsigned h, unsigned check_size) {
    std::vector<uint32_t> pixels(static_cast<std::size_t>(w) * h);
    constexpr uint32_t dark = 0xFF787878u;
    constexpr uint32_t light = 0xFFA0A0A0u;
    for (unsigned y = 0; y < h; ++y)
      for (unsigned x = 0; x < w; ++x) {
        const bool even = ((x / check_size) + (y / check_size)) % 2 == 0;
        pixels[static_cast<std::size_t>(y) * w + x] = even ? light : dark;
      }
    return impl_->textures_->create(w, h, pixels.data(), WrapMode::repeat);
  }

} // namespace corundum::tool_host
