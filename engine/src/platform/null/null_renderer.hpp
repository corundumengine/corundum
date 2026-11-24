// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/math/vec.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/platform/resource_ids.hpp>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace corundum::platform::null {

/// The texture id every load_texture() returns.
constexpr corundum::platform::TextureId k_dummy_texture{1};

/// The font id every load_font() returns; the null backend performs no real asset loading, so
/// every request aliases this.
constexpr corundum::platform::FontId k_dummy_font{1};

/** Assumed per-glyph advance width reported by measure_text(). */
constexpr float k_glyph_advance_px = 8.f;

/** No-op Renderer for headless lifecycle tests.
 *
 *  Satisfies the platform::Renderer interface without touching a GPU: every
 *  load returns a fixed dummy id, drawing is discarded, and measure_text()
 *  reports a rough per-character width.
 */
class NullRenderer final : public corundum::platform::Renderer {
public:
  [[nodiscard]] std::expected<corundum::platform::TextureId, std::string>
  load_texture(std::string_view /*path*/) override {
    return k_dummy_texture;
  }

  [[nodiscard]] std::expected<corundum::platform::FontId, std::string> load_font(std::string_view /*path*/) override {
    return k_dummy_font;
  }

  [[nodiscard]] core::math::Vec2 texture_size(corundum::platform::TextureId /*texture_id*/) const override {
    return {};
  }

  void release_texture(corundum::platform::TextureId /*texture_id*/) override {}

  void purge_unused_textures() override {}

  void set_world_view(core::math::Vec2 top_left, core::math::Vec2 /*viewport_size*/, float zoom) override {
    last_cam_ = top_left;
    last_zoom_ = zoom;
  }

  void reset_screen_view() override {}

  [[nodiscard]] bool begin_frame(core::math::Colour /*clear_colour*/) override { return true; }

  void end_frame() override {}

  void draw(const DrawSprite & /*cmd*/) override {}

  void draw(const DrawText & /*cmd*/) override {}

  void draw(const DrawRect & /*cmd*/) override {}

  void draw(const DrawLine & /*cmd*/) override {}

  [[nodiscard]] float measure_text(corundum::platform::FontId /*font_id*/, std::string_view text,
                                   uint32_t /*char_size*/) const override {
    return static_cast<float>(text.size()) * k_glyph_advance_px;
  }

  [[nodiscard]] RendererStats stats() const override { return {}; }

  /** Camera top-left recorded by the most recent set_world_view() call.
   *  @note Exposed so tests can observe the camera position render() computes.
   */
  [[nodiscard]] core::math::Vec2 last_camera_top_left() const noexcept { return last_cam_; }

  /** Zoom recorded by the most recent set_world_view() call.
   *  @note Exposed so tests can observe the zoom render() computes.
   */
  [[nodiscard]] float last_zoom() const noexcept { return last_zoom_; }

private:
  core::math::Vec2 last_cam_{};
  float last_zoom_{1.f};
};

} // namespace corundum::platform::null
