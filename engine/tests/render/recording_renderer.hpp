// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/math/vec.hpp>
#include <corundum/platform/renderer.hpp>

#include <algorithm>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace corundum::test {

  /// Renderer that records sprite draws and hands out a distinct texture id per path.
  class TileRecordingRenderer final : public corundum::platform::Renderer {
  public:
    std::vector<corundum::platform::DrawSprite> sprites;

    /// Paths load_texture() refuses with std::unexpected.
    std::vector<std::string> failing_paths;

    std::expected<uint32_t, std::string> load_texture(std::string_view path) override {
      const std::string key{path};
      if (std::ranges::find(failing_paths, key) != failing_paths.end())
        return std::unexpected("refused");
      const auto [it, inserted] = ids_.try_emplace(key, next_id_);
      if (inserted)
        ++next_id_;
      return it->second;
    }

    [[nodiscard]] corundum::core::math::Vec2 texture_size(uint32_t /*texture_id*/) const override { return {}; }

    void release_texture(uint32_t /*texture_id*/) override {}

    void purge_unused_textures() override {}

    std::expected<uint32_t, std::string> load_font(std::string_view /*path*/) override { return 1u; }

    void set_world_view(corundum::core::math::Vec2 /*top_left*/, corundum::core::math::Vec2 /*viewport_size*/,
                        float /*zoom*/) override {}

    void reset_screen_view() override {}

    [[nodiscard]] bool begin_frame(corundum::core::math::Colour /*clear_colour*/) override { return true; }

    void end_frame() override {}

    void draw(const corundum::platform::DrawSprite &cmd) override { sprites.push_back(cmd); }

    void draw(const corundum::platform::DrawText & /*cmd*/) override {}

    void draw(const corundum::platform::DrawRect & /*cmd*/) override {}

    void draw(const corundum::platform::DrawLine & /*cmd*/) override {}

    [[nodiscard]] float measure_text(uint32_t /*font_id*/, std::string_view /*text*/,
                                     uint32_t /*char_size*/) const override {
      return 0.f;
    }

    [[nodiscard]] corundum::platform::RendererStats stats() const override { return {}; }

  private:
    std::unordered_map<std::string, uint32_t> ids_;
    uint32_t next_id_{1};
  };

} // namespace corundum::test
