#pragma once
#include <corundum/core/math/vec.hpp>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace corundum::platform {

  /// Textured quad draw command.
  struct DrawSprite {
    uint32_t texture_id;
    core::math::Vec2 position;
    core::math::IntRect source;
    core::math::Vec2 scale{1.f, 1.f};
    bool flip_x{false};
    bool flip_y{false};
  };

  /// Rasterised text draw command.
  struct DrawText {
    uint32_t font_id;
    std::string_view text;
    core::math::Vec2 position;
    uint32_t char_size;
    core::math::Colour colour{255, 255, 255, 255};
  };

  /// Filled axis-aligned rectangle draw command.
  struct DrawRect {
    core::math::Vec2 position;
    core::math::Vec2 size;
    core::math::Colour colour;
  };

  /// Line segment draw command with configurable thickness.
  struct DrawLine {
    core::math::Vec2 start;
    core::math::Vec2 end;
    core::math::Colour colour{255, 255, 255, 255};
    float thickness{1.f};
  };

  /** @brief Platform-independent rendering interface.
   *
   * Resource IDs (textures, fonts) are opaque uint32_t handles. ID 0 is invalid.
   * Concrete implementations are created via factory functions
   * (e.g. make_sokol_renderer, make_null_renderer).
   *
   * @note Not thread-safe. Call only from the render thread.
   */
  class Renderer {
  public:
    virtual ~Renderer() = default;

    [[nodiscard]] virtual std::expected<uint32_t, std::string> load_texture(std::string_view path) = 0;

    [[nodiscard]] virtual std::expected<uint32_t, std::string> load_font(std::string_view path) = 0;

    virtual void set_world_view(core::math::Vec2 top_left, core::math::Vec2 viewport_size) = 0;

    virtual void reset_screen_view() = 0;

    virtual void begin_frame(core::math::Colour clear_colour) = 0;

    virtual void end_frame() = 0;

    virtual void draw(const DrawSprite &cmd) = 0;

    virtual void draw(const DrawText &cmd) = 0;

    virtual void draw(const DrawRect &cmd) = 0;

    virtual void draw(const DrawLine &cmd) = 0;

    [[nodiscard]] virtual float measure_text(uint32_t font_id, std::string_view text, uint32_t char_size) const = 0;
  };

} // namespace corundum::platform
