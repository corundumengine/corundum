#pragma once
#include <corundum/platform/renderer.hpp>

namespace corundum::platform::null {

  /// Single shared handle returned for every loaded texture/font; the null
  /// backend performs no real asset loading, so every request aliases this.
  constexpr uint32_t k_dummy_handle = 1u;
  /// Assumed per-glyph advance width used by measure_text().
  constexpr float k_glyph_advance_px = 8.f;

  /** @brief No-op Renderer for headless lifecycle tests.
   *
   * Satisfies the platform::Renderer interface without touching a GPU: every
   * load returns the same dummy handle, drawing is discarded, and
   * measure_text() reports a rough per-character width.
   */
  class NullRenderer final : public corundum::platform::Renderer {
  public:
    [[nodiscard]] std::expected<uint32_t, std::string> load_texture(std::string_view /*path*/) override {
      return k_dummy_handle;
    }

    [[nodiscard]] std::expected<uint32_t, std::string> load_font(std::string_view /*path*/) override {
      return k_dummy_handle;
    }

    void set_world_view(core::math::Vec2 /*top_left*/, core::math::Vec2 /*viewport_size*/, float /*zoom*/) override {}

    void reset_screen_view() override {}

    [[nodiscard]] bool begin_frame(core::math::Colour /*clear_colour*/) override {
      return true;
    }

    void end_frame() override {}

    void draw(const DrawSprite &/*cmd*/) override {}

    void draw(const DrawText &/*cmd*/) override {}

    void draw(const DrawRect &/*cmd*/) override {}

    void draw(const DrawLine &/*cmd*/) override {}

    [[nodiscard]] float measure_text(uint32_t /*font_id*/, std::string_view text, uint32_t /*char_size*/) const override {
      return static_cast<float>(text.size()) * k_glyph_advance_px;
    }

    [[nodiscard]] RendererStats stats() const override {
      return {};
    }
  };

} // namespace corundum::platform::null
