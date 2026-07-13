#pragma once
#include <corundum/platform/texture_cache.hpp>
#include <corundum/platform/window.hpp>

#include <expected>
#include <functional>
#include <imgui.h>
#include <memory>
#include <string>
#include <string_view>

namespace corundum::platform {
  class GpuContext;
}

namespace corundum::tool_host {

  /// Parameters for ToolHost creation.
  struct ToolHostDesc {
    int width;
    int height;
    std::string_view title;
  };

  /// RAII host owning the window, GPU context, texture cache, and ImGui/sokol_imgui lifecycle.
  /// Replaces the old ToolApp with engine APIs (create_window, GpuContext, TextureCache).
  class ToolHost {
  public:
    [[nodiscard]] static std::expected<std::unique_ptr<ToolHost>, std::string> create(const ToolHostDesc &desc);
    ~ToolHost();

    ToolHost(const ToolHost &) = delete;
    ToolHost &operator=(const ToolHost &) = delete;

    /// Run the main loop, calling @p frame_fn each frame after ImGui new frame.
    void run(std::function<void()> frame_fn);

    /// Request that the main loop exit after the current frame.
    void request_close();

    /// Set the OS window title.
    void set_title(std::string_view title);

    /// Access the platform window.
    [[nodiscard]] platform::Window &window();

    /// Access the texture cache.
    [[nodiscard]] platform::TextureCache &textures();

    /// Convert a texture ID to an ImTextureID for use with ImGui::Image.
    [[nodiscard]] ImTextureID imgui_id(uint32_t texture_id);

    /// Create a checkerboard pattern texture (RGBA8, REPEAT wrap).
    [[nodiscard]] platform::TextureInfo make_checkerboard(unsigned w, unsigned h, unsigned check_size = 8);

  private:
    ToolHost();
    struct Impl;
    std::unique_ptr<Impl> impl_;
  };

} // namespace corundum::tool_host
