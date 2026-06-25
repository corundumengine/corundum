#pragma once
#include <corundum/platform/renderer.hpp>

namespace corundum::platform::null {

  /** @brief Create a no-op Renderer for testing engine logic without a platform. */
  inline corundum::platform::Renderer make_null_renderer() {
    return corundum::platform::Renderer{
        .state = nullptr,
        ._destroy = nullptr,
        ._load_texture = [](void *, std::string_view) -> std::expected<uint32_t, std::string> { return 1u; },
        ._load_font = [](void *, std::string_view) -> std::expected<uint32_t, std::string> { return 1u; },
        ._set_world_view = [](void *, corundum::core::math::Vec2, corundum::core::math::Vec2) {},
        ._reset_screen_view = [](void *) {},
        ._begin_frame = [](void *, corundum::core::math::Colour) {},
        ._end_frame = [](void *) {},
        ._draw_sprite = [](void *, const corundum::platform::DrawSprite &) {},
        ._draw_text = [](void *, const corundum::platform::DrawText &) {},
        ._draw_rect = [](void *, const corundum::platform::DrawRect &) {},
        ._draw_line = [](void *, const corundum::platform::DrawLine &) {},
        ._measure_text = [](const void *, uint32_t, std::string_view text, uint32_t) -> float {
          return static_cast<float>(text.size()) * 8.f;
        },
    };
  }

} // namespace corundum::platform::null
