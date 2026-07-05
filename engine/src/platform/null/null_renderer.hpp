#pragma once
#include <corundum/platform/renderer.hpp>

#include <memory>

namespace corundum::platform::null {

  class NullRenderer final : public corundum::platform::Renderer {
  public:
    std::expected<uint32_t, std::string> load_texture(std::string_view) override {
      return 1u;
    }

    std::expected<uint32_t, std::string> load_font(std::string_view) override {
      return 1u;
    }

    void set_world_view(core::math::Vec2, core::math::Vec2, float) override {}

    void reset_screen_view() override {}

    void begin_frame(core::math::Colour) override {}

    void end_frame() override {}

    void draw(const DrawSprite &) override {}

    void draw(const DrawText &) override {}

    void draw(const DrawRect &) override {}

    void draw(const DrawLine &) override {}

    float measure_text(uint32_t, std::string_view text, uint32_t) const override {
      return static_cast<float>(text.size()) * 8.f;
    }
  };

  /** @brief Create a no-op Renderer for testing engine logic without a platform. */
  inline std::unique_ptr<corundum::platform::Renderer> make_null_renderer() {
    return std::make_unique<NullRenderer>();
  }

} // namespace corundum::platform::null
