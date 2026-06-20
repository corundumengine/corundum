#include "tool_app.hpp"

#include "../editors/common/tool_texture.hpp"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <sokol_gfx.h>
#include <sokol_imgui.h>
#include <stb_image.h>

#ifdef SOKOL_METAL
// Plain-C helpers defined in engine_platform_glfw (glfw_window_metal.mm).
extern "C" {
void *metal_setup_layer(struct GLFWwindow *win);
const void *metal_device(void *layer_ptr);
const void *metal_next_drawable(void *layer_ptr);
}
#endif

#include <chrono>
#include <format>
#include <stdexcept>
#include <string>
#include <vector>

namespace tools {

  struct ToolApp::Impl {
    GLFWwindow *win{};
#ifdef SOKOL_METAL
    void *metal_layer{};
#endif

    struct RegisteredTexture {
      sg_image image{};
      sg_sampler sampler{};
      sg_view view{};
    };
    std::vector<RegisteredTexture> textures_;
    uint32_t next_tex_id_{1};
  };

  ToolApp::ToolApp(int width, int height, std::string_view title) : impl_{std::make_unique<Impl>()} {
    if (!glfwInit())
      throw std::runtime_error("[ToolApp] glfwInit() failed");

#ifdef SOKOL_METAL
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
#endif

    impl_->win = glfwCreateWindow(width, height, std::string{title}.c_str(), nullptr, nullptr);
    if (!impl_->win) {
      glfwTerminate();
      throw std::runtime_error("[ToolApp] glfwCreateWindow() failed");
    }

#ifdef SOKOL_METAL
    impl_->metal_layer = metal_setup_layer(impl_->win);
    sg_setup(sg_desc{
        .environment =
            {
                .metal =
                    {
                        .device = metal_device(impl_->metal_layer),
                    },
            },
    });
#else
    glfwMakeContextCurrent(impl_->win);
    glfwSwapInterval(1);
    sg_setup(sg_desc{});
#endif

    // simgui_setup creates the ImGui context; imgui_impl_glfw requires
    // a current context, so it must be initialised after.
    // depth_format = NONE must match the swapchain (no depth buffer for 2D UI).
    simgui_setup(simgui_desc_t{
        .depth_format = SG_PIXELFORMAT_NONE,
    });

#ifdef SOKOL_METAL
    ImGui_ImplGlfw_InitForOther(impl_->win, true);
#else
    ImGui_ImplGlfw_InitForOpenGL(impl_->win, true);
#endif
  }

  ToolApp::~ToolApp() {
    if (!impl_)
      return;
    for (auto &t : impl_->textures_) {
      if (t.view.id != 0)
        sg_destroy_view(t.view);
      if (t.sampler.id != 0)
        sg_destroy_sampler(t.sampler);
      if (t.image.id != 0)
        sg_destroy_image(t.image);
    }
    ImGui_ImplGlfw_Shutdown();
    simgui_shutdown();
    sg_shutdown();
    if (impl_->win)
      glfwDestroyWindow(impl_->win);
    glfwTerminate();
  }

  void ToolApp::close() {
    if (impl_)
      glfwSetWindowShouldClose(impl_->win, GLFW_TRUE);
  }

  void ToolApp::set_title(std::string_view title) {
    if (impl_)
      glfwSetWindowTitle(impl_->win, std::string{title}.c_str());
  }

  ImTextureID ToolApp::tex_id(const common::ToolTexture &t) noexcept {
    if (t.id == 0 || t.id > impl_->textures_.size())
      return {};
    return simgui_imtextureid_with_sampler(impl_->textures_[t.id - 1].view, impl_->textures_[t.id - 1].sampler);
  }

  common::ToolTexture ToolApp::load_texture(std::string_view path) {
    const std::string path_str{path};
    int w{}, h{}, channels{};
    unsigned char *pixels = stbi_load(path_str.c_str(), &w, &h, &channels, STBI_rgb_alpha);
    if (!pixels)
      throw std::runtime_error(std::format("Cannot load texture: {}", path));

    sg_image_desc img_desc{};
    img_desc.width = w;
    img_desc.height = h;
    img_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    img_desc.data.mip_levels[0] = {.ptr = pixels, .size = static_cast<size_t>(w * h * 4)};
    const sg_image image = sg_make_image(img_desc);
    stbi_image_free(pixels);

    sg_sampler_desc smp_desc{};
    smp_desc.min_filter = SG_FILTER_NEAREST;
    smp_desc.mag_filter = SG_FILTER_NEAREST;
    smp_desc.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    smp_desc.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    const sg_sampler sampler = sg_make_sampler(smp_desc);

    sg_view_desc vdesc{};
    vdesc.texture.image = image;
    const sg_view view = sg_make_view(&vdesc);

    const uint32_t id = impl_->next_tex_id_++;
    impl_->textures_.push_back({image, sampler, view});
    return common::ToolTexture{.id = id, .w = static_cast<unsigned>(w), .h = static_cast<unsigned>(h)};
  }

  common::ToolTexture ToolApp::make_checkerboard(unsigned w, unsigned h, unsigned check_size) {
    std::vector<uint32_t> pixels(static_cast<size_t>(w) * h);
    constexpr uint32_t dark = 0xFF787878u;
    constexpr uint32_t light = 0xFFA0A0A0u;
    for (unsigned y = 0; y < h; ++y) {
      for (unsigned x = 0; x < w; ++x) {
        const bool even = ((x / check_size) + (y / check_size)) % 2 == 0;
        pixels[y * w + x] = even ? light : dark;
      }
    }

    sg_image_desc img_desc{};
    img_desc.width = static_cast<int>(w);
    img_desc.height = static_cast<int>(h);
    img_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    img_desc.data.mip_levels[0] = {.ptr = pixels.data(), .size = pixels.size() * sizeof(uint32_t)};
    const sg_image image = sg_make_image(img_desc);

    sg_sampler_desc smp_desc{};
    smp_desc.min_filter = SG_FILTER_NEAREST;
    smp_desc.mag_filter = SG_FILTER_NEAREST;
    smp_desc.wrap_u = SG_WRAP_REPEAT;
    smp_desc.wrap_v = SG_WRAP_REPEAT;
    const sg_sampler sampler = sg_make_sampler(smp_desc);

    sg_view_desc vdesc{};
    vdesc.texture.image = image;
    const sg_view view = sg_make_view(&vdesc);

    const uint32_t id = impl_->next_tex_id_++;
    impl_->textures_.push_back({image, sampler, view});
    return common::ToolTexture{.id = id, .w = w, .h = h};
  }

  common::ToolTexture ToolApp::create_texture(unsigned w, unsigned h, const uint32_t *pixels) {
    sg_image_desc img_desc{};
    img_desc.width = static_cast<int>(w);
    img_desc.height = static_cast<int>(h);
    img_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    img_desc.data.mip_levels[0] = {.ptr = pixels, .size = static_cast<size_t>(w) * h * sizeof(uint32_t)};
    const sg_image image = sg_make_image(img_desc);

    sg_sampler_desc smp_desc{};
    smp_desc.min_filter = SG_FILTER_NEAREST;
    smp_desc.mag_filter = SG_FILTER_NEAREST;
    smp_desc.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    smp_desc.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    const sg_sampler sampler = sg_make_sampler(smp_desc);

    sg_view_desc vdesc{};
    vdesc.texture.image = image;
    const sg_view view = sg_make_view(&vdesc);

    const uint32_t id = impl_->next_tex_id_++;
    impl_->textures_.push_back({image, sampler, view});
    return common::ToolTexture{.id = id, .w = w, .h = h};
  }

  void ToolApp::destroy_texture(common::ToolTexture &tex) noexcept {
    if (tex.id == 0 || tex.id > impl_->textures_.size())
      return;
    auto &t = impl_->textures_[tex.id - 1];
    sg_destroy_view(t.view);
    sg_destroy_sampler(t.sampler);
    sg_destroy_image(t.image);
    t = {};
    tex = {};
  }

  void ToolApp::run(std::function<void()> frame_fn) {
    using clock = std::chrono::steady_clock;
    auto prev = clock::now();

    while (!glfwWindowShouldClose(impl_->win)) {
      glfwPollEvents();

      auto now = clock::now();
      const float dt = std::chrono::duration<float>(now - prev).count();
      prev = now;

      int fb_w{}, fb_h{};
      glfwGetFramebufferSize(impl_->win, &fb_w, &fb_h);
      if (fb_w == 0 || fb_h == 0)
        continue;

      int win_w{}, win_h{};
      glfwGetWindowSize(impl_->win, &win_w, &win_h);
      const float dpi = win_w > 0 ? static_cast<float>(fb_w) / static_cast<float>(win_w) : 1.f;

      ImGui_ImplGlfw_NewFrame();
      simgui_new_frame(simgui_frame_desc_t{
          .width = fb_w,
          .height = fb_h,
          .delta_time = dt,
          .dpi_scale = dpi,
      });

      frame_fn();

      // --- Render pass ---
      sg_pass_action action{};
      action.colors[0] = {
          .load_action = SG_LOADACTION_CLEAR,
          .clear_value = {0.08f, 0.08f, 0.10f, 1.0f},
      };

#ifdef SOKOL_METAL
      const void *drawable = metal_next_drawable(impl_->metal_layer);
      if (!drawable) {
        ImGui::EndFrame();
        continue;
      }
      sg_swapchain swapchain{
          .width = fb_w,
          .height = fb_h,
          .sample_count = 1,
          .color_format = SG_PIXELFORMAT_BGRA8,
          .depth_format = SG_PIXELFORMAT_NONE,
          .metal =
              {
                  .current_drawable = drawable,
              },
      };
#else
      sg_swapchain swapchain{
          .width = fb_w,
          .height = fb_h,
          .sample_count = 1,
          .color_format = SG_PIXELFORMAT_RGBA8,
          .depth_format = SG_PIXELFORMAT_NONE,
      };
#endif

      sg_begin_pass(sg_pass{.action = action, .swapchain = swapchain});
      simgui_render();
      sg_end_pass();
      sg_commit();

#ifndef SOKOL_METAL
      glfwSwapBuffers(impl_->win);
#endif
    }
  }

} // namespace tools
