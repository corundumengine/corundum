#pragma once
#include <corundum/platform/renderer.hpp>

namespace corundum::platform::null {

  namespace {
    /// Single shared handle returned for every loaded texture/font; the null
    /// backend performs no real asset loading, so every request aliases this.
    constexpr uint32_t k_dummy_handle = 1u;
    /// Assumed per-glyph advance width used by measure_text().
    constexpr float k_glyph_advance_px = 8.f;
  } // namespace

  /** @brief No-op Renderer for headless lifecycle tests.
   *
   * Satisfies the platform::Renderer interface without touching a GPU: every
   * load returns the same dummy handle, drawing is discarded, and
   * measure_text() reports a rough per-character width.
   */
  class NullRenderer final : public corundum::platform::Renderer {
  public:
    std::expected<uint32_t, std::string> load_texture(std::string_view) override {
      return k_dummy_handle;
    }

    std::expected<uint32_t, std::string> load_font(std::string_view) override {
      return k_dummy_handle;
    }

    void set_world_view(core::math::Vec2, core::math::Vec2, float) override {}

    void reset_screen_view() override {}

    bool begin_frame(core::math::Colour) override {
      return true;
    }

    void end_frame() override {}

    void draw(const DrawSprite &) override {}

    void draw(const DrawText &) override {}

    void draw(const DrawRect &) override {}

    void draw(const DrawLine &) override {}

    float measure_text(uint32_t, std::string_view text, uint32_t) const override {
      return static_cast<float>(text.size()) * k_glyph_advance_px;
    }

    RendererStats stats() const override {
      return {};
    }
  };

} // namespace corundum::platform::null
