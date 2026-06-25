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

  /** @brief Platform-independent rendering interface — struct of function pointers.
   *
   * Resource IDs (textures, fonts) are opaque uint32_t handles. ID 0 is invalid.
   * Concrete implementations populate the function pointers via factory functions
   * (e.g. make_sokol_renderer, make_null_renderer). All function pointers are
   * non-null after construction.
   *
   * Inline convenience methods preserve the same call-site API as before so
   * existing render code requires no changes.
   *
   * @note Not thread-safe. Call only from the render thread.
   * @performance No virtual dispatch — function pointer calls through the struct
   *              with explicit state, avoiding vtable indirection.
   */
  struct Renderer {
    void *state{nullptr};
    void (*_destroy)(void *){nullptr};

    using LoadTexFn = std::expected<uint32_t, std::string> (*)(void *, std::string_view);
    using LoadFontFn = std::expected<uint32_t, std::string> (*)(void *, std::string_view);
    using ViewFn = void (*)(void *, core::math::Vec2, core::math::Vec2);
    using VoidFn = void (*)(void *);
    using ColourFn = void (*)(void *, core::math::Colour);
    using DrawSprFn = void (*)(void *, const DrawSprite &);
    using DrawTxtFn = void (*)(void *, const DrawText &);
    using DrawRectFn = void (*)(void *, const DrawRect &);
    using MeasureFn = float (*)(const void *, uint32_t, std::string_view, uint32_t);

    LoadTexFn _load_texture{nullptr};
    LoadFontFn _load_font{nullptr};
    ViewFn _set_world_view{nullptr};
    VoidFn _reset_screen_view{nullptr};
    ColourFn _begin_frame{nullptr};
    VoidFn _end_frame{nullptr};
    DrawSprFn _draw_sprite{nullptr};
    DrawTxtFn _draw_text{nullptr};
    DrawRectFn _draw_rect{nullptr};
    /**
     * @brief Function pointer type for drawing a line segment.
     */
    using DrawLineFn = void (*)(void *, const DrawLine &);
    DrawLineFn _draw_line{nullptr};
    MeasureFn _measure_text{nullptr};

    [[nodiscard]] auto load_texture(std::string_view path) {
      return _load_texture(state, path);
    }

    [[nodiscard]] auto load_font(std::string_view path) {
      return _load_font(state, path);
    }

    void set_world_view(core::math::Vec2 top_left, core::math::Vec2 viewport_size) {
      _set_world_view(state, top_left, viewport_size);
    }

    void reset_screen_view() {
      _reset_screen_view(state);
    }

    void begin_frame(core::math::Colour clear_colour) {
      _begin_frame(state, clear_colour);
    }

    void end_frame() {
      _end_frame(state);
    }

    void draw(const DrawSprite &cmd) {
      _draw_sprite(state, cmd);
    }

    void draw(const DrawText &cmd) {
      _draw_text(state, cmd);
    }

    void draw(const DrawRect &cmd) {
      _draw_rect(state, cmd);
    }

    void draw(const DrawLine &cmd) {
      _draw_line(state, cmd);
    }

    [[nodiscard]] float measure_text(uint32_t font_id, std::string_view text, uint32_t char_size) const {
      return _measure_text(state, font_id, text, char_size);
    }
  };

} // namespace corundum::platform
