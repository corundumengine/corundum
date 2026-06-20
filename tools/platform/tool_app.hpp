#pragma once
#include <functional>
#include <imgui.h>
#include <memory>
#include <string_view>

namespace tools {

namespace common {
  struct ToolTexture;
}

/// RAII helper that owns a GLFW window with sokol_gfx and sokol_imgui initialized.
///
/// Construction initialises GLFW, the platform graphics context (Metal or OpenGL),
/// sokol_gfx, sokol_imgui, and imgui_impl_glfw. Destruction shuts everything down
/// in reverse order.
///
/// @note Not thread-safe. Use only from the main thread.
struct ToolApp {
  /// @throws std::runtime_error if the window or graphics context cannot be created.
  explicit ToolApp(int width, int height, std::string_view title);
  ~ToolApp();

  ToolApp(const ToolApp &) = delete;
  ToolApp &operator=(const ToolApp &) = delete;

  /// Runs the main loop until the window is closed.
  ///
  /// Calls @p frame_fn once per iteration after simgui_new_frame/ImGui::NewFrame,
  /// immediately before the sokol render pass and simgui_render.
  void run(std::function<void()> frame_fn);

  /// Request the window to close at the end of the current frame.
  void close();

  /// Set the OS window title.
  void set_title(std::string_view title);

  /// @return ImTextureID for use with ImGui::Image.
  [[nodiscard]] ImTextureID tex_id(const common::ToolTexture &t) noexcept;

  /// Load a PNG/JPG from disk into a GPU texture.
  /// @throws std::runtime_error on load failure.
  [[nodiscard]] common::ToolTexture load_texture(std::string_view path);

  /// Build a checkerboard RGBA8 texture.
  [[nodiscard]] common::ToolTexture make_checkerboard(unsigned w, unsigned h, unsigned check_size = 8);

  /// Create a GPU texture from raw RGBA8 pixel data.
  [[nodiscard]] common::ToolTexture create_texture(unsigned w, unsigned h, const uint32_t *pixels);

  /// Release GPU resources for @p tex.
  void destroy_texture(common::ToolTexture &tex) noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace tools
