#pragma once
#include <cstdint>
#include <imgui.h>
#include <string_view>

namespace tools {

  struct ToolApp;

  namespace common {

    /// GPU texture handle — opaque ID managed by ToolApp.
    ///
    /// Lifetime: must not outlive the ToolApp that created it.
    struct ToolTexture {
      uint32_t id{}; ///< Opaque handle into ToolApp's texture registry.
      unsigned w{};  ///< Texture width in pixels.
      unsigned h{};  ///< Texture height in pixels.
    };

    /// @return ImTextureID for use with ImGui::Image / ImDrawList::AddImage.
    [[nodiscard]] ImTextureID tex_id(ToolApp &app, const ToolTexture &t) noexcept;

    /// Load a PNG/JPG/etc. from disk into a GPU texture via stb_image + sokol_gfx.
    /// @throws std::runtime_error on load failure.
    [[nodiscard]] ToolTexture load_tool_texture(ToolApp &app, std::string_view path);

    /// Build a checkerboard RGBA8 texture of @p w × @p h pixels with @p check_size squares.
    [[nodiscard]] ToolTexture make_checkerboard(ToolApp &app, unsigned w, unsigned h, unsigned check_size = 8);

    /// Create a GPU texture from raw RGBA8 pixel data.
    [[nodiscard]] ToolTexture create_tool_texture(ToolApp &app, unsigned w, unsigned h, const uint32_t *pixels);

    /// Release the GPU resources owned by @p tex and zero out the handle.
    void destroy_tool_texture(ToolApp &app, ToolTexture &tex) noexcept;

  } // namespace common
} // namespace tools
