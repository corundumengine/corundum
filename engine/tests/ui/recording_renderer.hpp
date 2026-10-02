// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/math/vec.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/nine_patch.hpp>

#include <deque>
#include <expected>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace corundum::test {

  /// Records every draw call into one ordered log so tests can assert both counts and
  /// ordering. measure_text mirrors the null backend's per-glyph width so tests don't
  /// depend on real font metrics.
  class RecordingRenderer final : public corundum::platform::Renderer {
  public:
    using DrawCall = std::variant<platform::DrawRect, platform::DrawSprite, platform::DrawText>;

    std::vector<DrawCall> log{};
    // DrawText::text is a string_view that may point into temporaries that die when the render
    // call returns (e.g. formatted inventory labels); the real renderer consumes it
    // synchronously, but this recorder must keep it alive. A deque (not vector) keeps
    // references to stored strings stable across push_back, since the recorded DrawText views
    // point into this container.
    std::deque<std::string> text_owner{};

    std::expected<uint32_t, std::string> load_texture(std::string_view /*path*/) override {
      return 1u;
    }

    std::expected<uint32_t, std::string> load_font(std::string_view /*path*/) override {
      return 2u;
    }

    void set_world_view(core::math::Vec2 /*top_left*/, core::math::Vec2 /*viewport_size*/, float /*zoom*/) override {}

    void reset_screen_view() override {}

    bool begin_frame(core::math::Colour /*clear_colour*/) override {
      return true;
    }

    void end_frame() override {}

    void draw(const platform::DrawSprite &cmd) override {
      log.emplace_back(cmd);
    }

    void draw(const platform::DrawText &cmd) override {
      text_owner.emplace_back(cmd.text);
      platform::DrawText copy = cmd;
      copy.text = text_owner.back();
      log.emplace_back(copy);
    }

    void draw(const platform::DrawRect &cmd) override {
      log.emplace_back(cmd);
    }

    void draw(const platform::DrawLine & /*cmd*/) override {}

    [[nodiscard]] float measure_text(uint32_t /*font_id*/, std::string_view text,
                                     uint32_t /*char_size*/) const override {
      return static_cast<float>(text.size()) * 8.f;
    }

    [[nodiscard]] platform::RendererStats stats() const override {
      return {};
    }
  };

  inline corundum::ui::NinePatchBorder make_border() {
    corundum::ui::NinePatchBorder border{};
    border.texture_id = 1u;
    border.tile_w = 4;
    border.tile_h = 4;
    return border;
  }

} // namespace corundum::test
