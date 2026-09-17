// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/math/vec.hpp>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace corundum::platform {

  /// Textured quad draw command.
  struct DrawSprite {
    uint32_t texture_id{};

    core::math::Vec2 position{};

    core::math::IntRect source{};

    core::math::Vec2 scale{.x = 1.f, .y = 1.f};

    bool flip_x{false};

    bool flip_y{false};
  };

  /** @brief Rasterised text draw command.
   *
   *  @note @c text is borrowed for the duration of the draw call only; the renderer does
   *        not retain it.
   */
  struct DrawText {
    uint32_t font_id{0};

    std::string_view text{};

    core::math::Vec2 position{};

    uint32_t char_size{0};

    core::math::Colour colour{.r = 255, .g = 255, .b = 255, .a = 255};
  };

  /// Filled axis-aligned rectangle draw command.
  struct DrawRect {
    core::math::Vec2 position{};

    core::math::Vec2 size{};

    core::math::Colour colour{};
  };

  /// Line segment draw command with configurable thickness.
  struct DrawLine {
    core::math::Vec2 start{};

    core::math::Vec2 end{};

    core::math::Colour colour{.r = 255, .g = 255, .b = 255, .a = 255};

    float thickness{1.f};
  };

  /// Per-frame rendering diagnostics. Best-effort, not a contract.
  struct RendererStats {
    uint32_t draw_calls{};

    uint32_t quads{};

    uint32_t dropped_quads{};
  };

  /** @brief Platform-independent rendering interface.
   *
   * Resource IDs (textures, fonts) are opaque uint32_t handles. ID 0 is invalid.
   * Concrete implementations are created via the platform factory
   * (create_platform() in platform_factory.hpp).
   *
   * Draw commands are only consumed inside a begin_frame()/end_frame() bracket; draws
   * issued outside one, or referencing an invalid resource ID, are discarded.
   *
   * @note Not thread-safe. Call only from the render thread.
   */
  class Renderer {
  public:
    virtual ~Renderer() = default;

    /// Load a texture from an image file.
    /// @return Opaque texture ID, or std::unexpected with the reason.
    [[nodiscard]] virtual std::expected<uint32_t, std::string> load_texture(std::string_view path) = 0;

    /// Load a font face for rasterised text.
    /// @return Opaque font ID, or std::unexpected with the reason.
    [[nodiscard]] virtual std::expected<uint32_t, std::string> load_font(std::string_view path) = 0;

    /// Set the world camera rect (top-left and viewport size, in world pixels) and zoom.
    /// Subsequent draws are interpreted in world space until reset_screen_view().
    virtual void set_world_view(core::math::Vec2 top_left, core::math::Vec2 viewport_size, float zoom) = 0;

    /// Return to screen-space drawing.
    virtual void reset_screen_view() = 0;

    /** @brief Start a frame's default render pass, clearing to @p clear_colour.
     *
     *  @return @c true if the pass was started; @c false if the frame was skipped because
     *          no render target was ready, in which case the caller must issue no draws this
     *          frame and may skip end_frame().
     */
    [[nodiscard]] virtual bool begin_frame(core::math::Colour clear_colour) = 0;

    /// End the pass started by begin_frame() and present the frame. No-op if the frame was skipped.
    virtual void end_frame() = 0;

    virtual void draw(const DrawSprite &cmd) = 0;

    virtual void draw(const DrawText &cmd) = 0;

    virtual void draw(const DrawRect &cmd) = 0;

    virtual void draw(const DrawLine &cmd) = 0;

    /// Advance width in pixels of @p text at @p char_size, or 0 if @p font_id resolves to no font.
    [[nodiscard]] virtual float measure_text(uint32_t font_id, std::string_view text, uint32_t char_size) const = 0;

    [[nodiscard]] virtual RendererStats stats() const = 0;
  };

} // namespace corundum::platform
